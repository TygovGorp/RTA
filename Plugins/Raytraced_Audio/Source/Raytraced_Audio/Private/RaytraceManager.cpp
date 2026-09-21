#include "RaytraceManager.h"

#include "AcousticMaterialAsset.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Log.h"

FRaytraceManager::FRaytraceManager() : World(nullptr)
{
	UE_LOG(LogRTA, Log, TEXT("Create New Manager"))
	
	AudioTraceDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FRaytraceManager::TickAudioTrace), 
		AudioTraceTickInterval);
}
FRaytraceManager::~FRaytraceManager()
{
	FTSTicker::GetCoreTicker().RemoveTicker(AudioTraceDelegateHandle);
}

void FRaytraceManager::RegisterSource(uint32 SourceId,
	float AirAbsorptionMinDistance,
	float AirAbsorptionMaxDistance,
	float AirAbsorptionCutoffAtMinDistance,
	float AirAbsorptionCutoffAtMaxDistance)
{
	TSharedPtr<FSourceRayData> NewData = MakeShared<FSourceRayData>();
	NewData->AirAbsorptionMinDistance = AirAbsorptionMinDistance;
	NewData->AirAbsorptionMaxDistance = AirAbsorptionMaxDistance;
	NewData->AirAbsorptionCutoffAtMinDistance = AirAbsorptionCutoffAtMinDistance;
	NewData->AirAbsorptionCutoffAtMaxDistance = AirAbsorptionCutoffAtMaxDistance;

	this->ResultsLock.WriteLock();
	if (Results.Find(SourceId) != nullptr) UE_LOG(LogRTA, Warning, TEXT("SourceId already present in Results Map"));
	this->Results.Add(SourceId, NewData);
	this->ResultsLock.WriteUnlock();
}

void FRaytraceManager::UnregisterSource(uint32 SourceId)
{
	this->ResultsLock.WriteLock();
	this->Results.Remove(SourceId);
	this->ResultsLock.WriteUnlock();
}

void FRaytraceManager::UpdateEmitterPosition(uint32 SourceId, const FVector& Position)
{
	ResultsLock.ReadLock();
	const auto Result = this->Results.Find(SourceId);
	if (Result == nullptr) { ResultsLock.ReadUnlock(); return; }
	TSharedPtr<FSourceRayData> RayData = *Result;
	ResultsLock.ReadUnlock();

	RayData->Lock.WriteLock();
	bool bIsFirstSet = !RayData->bEmitterPositionSet;
	bool bChanged = bIsFirstSet || FVector::DistSquared(RayData->EmitterPosition, Position) > FMath::Square(MinPositionDeltaForDirty);
	if (bChanged)
	{
		RayData->EmitterPosition = Position;
		RayData->bEmitterPositionSet = true;
		RayData->bDirty = true;
	}
	RayData->Lock.WriteUnlock();
}

void FRaytraceManager::UpdateListenerPosition(uint32 SourceId, const FVector& Position)
{
	ResultsLock.ReadLock();
	const auto Result = this->Results.Find(SourceId);
	if (Result == nullptr) { ResultsLock.ReadUnlock(); return; }
	TSharedPtr<FSourceRayData> RayData = *Result;
	ResultsLock.ReadUnlock();

	RayData->Lock.WriteLock();
	bool bIsFirstSet = !RayData->bListenerPositionSet;
	bool bChanged = bIsFirstSet || FVector::DistSquared(RayData->ListenerPosition, Position) > FMath::Square(MinPositionDeltaForDirty);
	if (bChanged)
	{
		RayData->ListenerPosition = Position;
		RayData->bListenerPositionSet = true;
		RayData->bDirty = true;
	}
	RayData->Lock.WriteUnlock();
}

FRaytraceManager::FSourceRayData FRaytraceManager::GetLatestResults(uint32 SourceId)
{
	this->ResultsLock.ReadLock();
	const auto Result = this->Results.Find(SourceId);
	if (Result == nullptr)
	{
		this->ResultsLock.ReadUnlock();
		return FSourceRayData();
	}
	
	Result->Get()->Lock.ReadLock();
	FSourceRayData LatestResult = *Result->Get(); 
	Result->Get()->Lock.ReadUnlock();
	this->ResultsLock.ReadUnlock();
	
	return LatestResult;
}

bool FRaytraceManager::TickAudioTrace(float /*DeltaTime*/)
{
	TArray<uint32> SourceIdsToTrace;

	ResultsLock.ReadLock();
	for (const TTuple<uint32, TSharedPtr<FSourceRayData>>& Result : Results)
	{
		if (Result.Value->bDirty 
			&& Result.Value->bListenerPositionSet 
			&& Result.Value->bEmitterPositionSet
			&& !Result.Value->bTraceInFlight)
		{
			Result.Value->Lock.WriteLock();
			Result.Value->bDirty = false;
			Result.Value->bTraceInFlight = true;
			Result.Value->Lock.WriteUnlock();
			SourceIdsToTrace.Add(Result.Key);
		}
	}
	ResultsLock.ReadUnlock();
	
	ThreadSafeCounter.Add(1);
	const uint32 TraceSeed = ThreadSafeCounter.GetValue();

	TWeakPtr<FRaytraceManager, ESPMode::ThreadSafe> WeakSelf = AsWeak();
	UE::Tasks::Launch(TEXT("RTA_AudioTrace"), [WeakSelf, SourceIdsToTrace, TraceSeed]()
	{
		if (TSharedPtr<FRaytraceManager, ESPMode::ThreadSafe> Self = WeakSelf.Pin())
		{
			Self->RunAudioTrace(SourceIdsToTrace, TraceSeed);
		}
	}, LowLevelTasks::ETaskPriority::BackgroundLow);
	
	return true;
}


TSharedPtr<FRaytraceManager::FSourceRayData> FRaytraceManager::FindSourceRayData(uint32 SourceId)
{
    FReadScopeLock Lock(ResultsLock);
    TSharedPtr<FSourceRayData>* Entry = Results.Find(SourceId);
    return Entry ? *Entry : nullptr;
}

void FRaytraceManager::RunAudioTrace(const TArray<uint32>& SourceIds, const uint32 TraceSeed)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RTA_RunAudioTrace);
    if (SourceIds.Num() == 0) return;

    TSharedPtr<FSourceRayData> ListenerData = FindSourceRayData(SourceIds[0]);
    if (!ListenerData) return;

    FVector ListenerPos;
    {
        FReadScopeLock Lock(ListenerData->Lock);
        ListenerPos = ListenerData->ListenerPosition;
    }
	
	TArray<FValidData> ValidData;
	float BatchMaxDistance = 0.f;
	
    FCollisionQueryParams DirectTraceParams;
    DirectTraceParams.bReturnPhysicalMaterial = true;
	DirectTraceParams.bTraceComplex = true; 

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RTA_DirectTraces);
		for (uint32 SourceId : SourceIds)
		{
			TSharedPtr<FSourceRayData> RayData = FindSourceRayData(SourceId);
			if (!RayData) continue;

			FVector EmitterPosition;
			{
				FReadScopeLock Lock(RayData->Lock);
				EmitterPosition = RayData->EmitterPosition;
			}

			FHitResult DirectHit;
			if (!World.IsValid() || !World->GetPhysicsScene() || World->GetPhysicsScene()->GetSolver() == nullptr)
			{
				RayData->bTraceInFlight = false; 
				return;
			}
			const bool bBlocked = World->LineTraceSingleByChannel(DirectHit, ListenerPos, EmitterPosition, ECC_Visibility, DirectTraceParams);

			FBandEnergy DirectEnergy; // defaults to {1,1,1} fully audible, unblocked case

			if (bBlocked)
			{
				if (const UAcousticMaterialAsset* Mat = GetAcousticMaterialAsset(DirectHit))
				{
					TArrayView<const float> Transmission = Mat->GetTransmission();
					for (int32 Band = 0; Band < RTA::NumBands; ++Band)
					{
						DirectEnergy.Bands[Band] = Transmission[Band];
					}
				}
				else
				{
					DirectEnergy = FBandEnergy{ {0.f, 0.f, 0.f} }; // untagged geometry: assume fully blocking
				}
			}

			const float Distance = FVector::Dist(ListenerPos, EmitterPosition);

			float CutoffHz;
			{
				FReadScopeLock Lock(RayData->Lock);
				const float MinDist = RayData->AirAbsorptionMinDistance;
				const float MaxDist = FMath::Max(RayData->AirAbsorptionMaxDistance, MinDist + KINDA_SMALL_NUMBER);
				const float Alpha = FMath::Clamp((Distance - MinDist) / (MaxDist - MinDist), 0.f, 1.f);
				CutoffHz = FMath::Lerp(RayData->AirAbsorptionCutoffAtMinDistance, RayData->AirAbsorptionCutoffAtMaxDistance, Alpha);
			}

			ValidData.Add({.SourceData = RayData, .SourceId = SourceId, .EmitterPos = EmitterPosition, .DirectLOS = !bBlocked, .DirectTransmissionEnergy = DirectEnergy, .AirAbsorptionCutoffHz = CutoffHz});
			BatchMaxDistance = FMath::Max(BatchMaxDistance, Distance);
		}
	}

    const int32 NumSources = ValidData.Num();
    if (NumSources == 0) return;

    const float DynamicMaxRayLength = FMath::Max(MaxRayLength, BatchMaxDistance * 5.5f);

    TArray<FLossAccumulator> TotalLoss;
    TotalLoss.Init(FLossAccumulator(), NumSources);

    FCollisionQueryParams CollisionQueryParams;
    CollisionQueryParams.bReturnPhysicalMaterial = true;
	CollisionQueryParams.bTraceComplex = true;
	
	BounceRaysTrace(ListenerPos, ValidData, TraceSeed, DynamicMaxRayLength, NumSources, TotalLoss);
	
    constexpr float SmoothingAlpha = 0.15f;
    for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
    {
        TraceWriteBack(ValidData, TotalLoss, SmoothingAlpha, SrcIdx);
    }
}

void FRaytraceManager::BounceRaysTrace(const FVector& ListenerPos, TArray<FValidData>& ValidData, const uint32& TraceSeed, const float& DynamicMaxRayLength, int32 NumSources, TArray<FLossAccumulator>& TotalLoss)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RTA_BounceRays);
	
	FRandomStream RndStrm;
	for (int32 RayNum = 0; RayNum < RayCount; ++RayNum)
	{
		FVector SegmentStart = ListenerPos;
		FVector PreviousSegmentStart = ListenerPos;
		FVector LastHitNormal = FVector::ZeroVector;
		float LastHitScattering = 0.5f;
		bool bHasHitSurface = false;

		float RayEnergy[RTA::NumBands] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
		RndStrm.Initialize(HashCombine(TraceSeed, RayNum));

		for (int32 Depth = 0; Depth < MaxDepth; ++Depth)
		{
			FVector RandomDir;

			if (!bHasHitSurface)
			{
				RandomDir = RndStrm.VRand();
			}
			else
			{
				const float ScatterRoll = RndStrm.FRand();
				if (ScatterRoll < LastHitScattering)
				{
					RandomDir = RandomCosineWeightedHemisphere(LastHitNormal, RndStrm);
				}
				else
				{
					const FVector IncomingDir = (SegmentStart - PreviousSegmentStart).GetSafeNormal();
					RandomDir = FMath::GetReflectionVector(IncomingDir, LastHitNormal);
				}
			}

			const FVector TraceEnd = SegmentStart + RandomDir * DynamicMaxRayLength;

			FHitResult HitResult;
			FCollisionQueryParams CollisionQueryParams;
			CollisionQueryParams.bReturnPhysicalMaterial = true;
			CollisionQueryParams.bTraceComplex = true;
			World->LineTraceSingleByChannel(HitResult, SegmentStart, TraceEnd, ECC_Visibility, CollisionQueryParams);
			if (!HitResult.IsValidBlockingHit())
						break;
	
			float HitScattering = 0.5f;

			if (const UAcousticMaterialAsset* Mat = GetAcousticMaterialAsset(HitResult))
			{
				TArrayView<const float> Absorption = Mat->GetAbsorption();
				TArrayView<const float> Transmission = Mat->GetTransmission();

				for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				{
					const float Reflected = 1.f - Absorption[Band] - Transmission[Band];
					RayEnergy[Band] *= FMath::Clamp(Reflected, 0.f, 1.f);
				}

				HitScattering = Mat->GetBakedScattering();
			}

			constexpr float SurfaceBias = 1.f;
			PreviousSegmentStart = SegmentStart;
			SegmentStart = HitResult.Location + HitResult.Normal * SurfaceBias;
			LastHitNormal = HitResult.Normal;
			LastHitScattering = HitScattering;
			bHasHitSurface = true;

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RTA_BounceNeeRays);
				for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
				{
					if (ValidData[SrcIdx].DirectLOS)
						continue;

					FHitResult NEEHitResult;
					const bool bNEEHit = World->LineTraceSingleByChannel(
						NEEHitResult, SegmentStart, ValidData[SrcIdx].EmitterPos, ECC_Visibility);

					if (!bNEEHit)
					{
						for (int32 Band = 0; Band < RTA::NumBands; ++Band)
						{
							TotalLoss[SrcIdx].Bands[Band] += (1.f - RayEnergy[Band]);
						}
						ValidData[SrcIdx].DirectLOS = true;
					}
				}
			}
		}

		for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
		{
			if (!ValidData[SrcIdx].DirectLOS)
			{
				for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				{
					TotalLoss[SrcIdx].Bands[Band] += 1.f;
				}
			}
		}
	}
}

void FRaytraceManager::TraceWriteBack(TArray<FValidData> ValidData, TArray<FLossAccumulator> TotalLoss, const float SmoothingAlpha, const int32 SrcIdx) const
{
	TSharedPtr<FSourceRayData>& RayData = ValidData[SrcIdx].SourceData;

	FWriteScopeLock Lock(RayData->Lock);
	for (int32 Band = 0; Band < RTA::NumBands; ++Band)
	{
		const float IndirectLoss = TotalLoss[SrcIdx].Bands[Band] / float(RayCount);
		const float IndirectEnergy = 1.f - IndirectLoss;
		const float DirectEnergy = ValidData[SrcIdx].DirectTransmissionEnergy.Bands[Band];
        	
		const float FinalEnergy = DirectEnergy + (1.f - DirectEnergy) * IndirectEnergy;
		const float NewEstimate = 1.f - FinalEnergy;

		if (!RayData->bHasValidEstimate)
		{
			RayData->DirectTransmissionLoss[Band] = NewEstimate;
		}
		else
		{
			RayData->DirectTransmissionLoss[Band] = FMath::Lerp(RayData->DirectTransmissionLoss[Band], NewEstimate, SmoothingAlpha);
		}
	}

	if (!RayData->bHasValidEstimate)
	{
		RayData->DirectLowpassCutoffHz = ValidData[SrcIdx].AirAbsorptionCutoffHz;
		RayData->bHasValidEstimate = true;
	}
	else
	{
		RayData->DirectLowpassCutoffHz = FMath::Lerp(RayData->DirectLowpassCutoffHz, ValidData[SrcIdx].AirAbsorptionCutoffHz, SmoothingAlpha);
	}

	UE_LOG(LogRTA, Log, TEXT("SourceId=%u Loss=[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f] CutoffHz=%.0f"),
		   ValidData[SrcIdx].SourceId,
		   RayData->DirectTransmissionLoss[0], RayData->DirectTransmissionLoss[1], RayData->DirectTransmissionLoss[2], RayData->DirectTransmissionLoss[3], RayData->DirectTransmissionLoss[4], RayData->DirectTransmissionLoss[5],
		   RayData->DirectLowpassCutoffHz);
    	
	RayData->bTraceInFlight = false;
}

FVector FRaytraceManager::RandomCosineWeightedHemisphere(const FVector& Normal, FRandomStream& RndStrm)
{
	float u1 = RndStrm.FRand();
	float u2 = RndStrm.FRand();
	float r = FMath::Sqrt(u1);
	float theta = 2.f * PI * u2;

	float x = r * FMath::Cos(theta);
	float y = r * FMath::Sin(theta);
	float z = FMath::Sqrt(FMath::Max(0.f, 1.f - u1));

	FVector Up = FMath::Abs(Normal.Z) < 0.999f ? FVector(0, 0, 1) : FVector(1, 0, 0);
	FVector Tangent = FVector::CrossProduct(Up, Normal).GetSafeNormal();
	FVector Bitangent = FVector::CrossProduct(Normal, Tangent);

	return (Tangent * x + Bitangent * y + Normal * z).GetSafeNormal();
}

const UAcousticMaterialAsset* FRaytraceManager::GetAcousticMaterialAsset(const FHitResult& HitResult)
{
	if (const UAcousticPhysicalMaterial* AcousticPhysMat = Cast<UAcousticPhysicalMaterial>(HitResult.PhysMaterial.Get()))
		if (const UAcousticMaterialAsset* Mat = AcousticPhysMat->AcousticMaterial.Get())
			return Mat;
		
	return nullptr;
}
