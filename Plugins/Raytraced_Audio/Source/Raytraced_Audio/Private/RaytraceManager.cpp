#include "RaytraceManager.h"

#include "AcousticMaterialAsset.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Log.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Tasks/Task.h"
#include "EchogramXlsx.h"
#include "RTADecayMetrics.h"
#include "Misc/DateTime.h"


FRaytraceManager::FRaytraceManager() : World(nullptr)
{
	UE_LOG(LogRTA, Log, TEXT("Create New Manager"));

	Listener = MakeShared<FListenerData>();

	AudioTraceDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FRaytraceManager::TickAudioTrace),
		AudioTraceTickInterval);

	RoomProbeDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FRaytraceManager::TickRoomProbe),
		RoomProbeTickInterval);
}

FRaytraceManager::~FRaytraceManager()
{
	FTSTicker::GetCoreTicker().RemoveTicker(AudioTraceDelegateHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(RoomProbeDelegateHandle);
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

	FWriteScopeLock Lock(ResultsLock);
	if (Results.Find(SourceId) != nullptr)
	{
		UE_LOG(LogRTA, Warning, TEXT("SourceId %u already present in Results map"), SourceId);
	}
	Results.Add(SourceId, NewData);
}

void FRaytraceManager::UnregisterSource(uint32 SourceId)
{
	FWriteScopeLock Lock(ResultsLock);
	Results.Remove(SourceId);
}

void FRaytraceManager::UpdateEmitterPosition(uint32 SourceId, const FVector& Position)
{
	TSharedPtr<FSourceRayData> RayData = FindSourceRayData(SourceId);
	if (!RayData.IsValid()) return;

	FWriteScopeLock Lock(RayData->Lock);
	const bool bFirstSet = !RayData->bEmitterPositionSet;
	if (bFirstSet ||
		FVector::DistSquared(RayData->EmitterPosition, Position) > FMath::Square(MinPositionDeltaForDirty))
	{
		RayData->EmitterPosition = Position;
		RayData->bEmitterPositionSet = true;
		RayData->bDirty = true;
	}
}

void FRaytraceManager::UpdateListenerPosition(const FVector& Position)
{
	if (!Listener.IsValid()) return;

	FWriteScopeLock Lock(Listener->Lock);

	const bool bFirstSet = !Listener->bPositionSet;
	const float DistSqCm = bFirstSet ? 0.f : FVector::DistSquared(Listener->Position, Position);

	if (!bFirstSet && DistSqCm > FMath::Square(RTA::TeleportThresholdCm))
	{
		Listener->AccumulatedEchogram.Reset();
		Listener->ProbeCount = 0;
		Listener->bHasValidEstimate = false;
		Listener->Result.Write(FRoomResult());
		UE_LOG(LogRTA, Log, TEXT("Listener teleported; echogram reset."));
	}

	if (bFirstSet || DistSqCm > FMath::Square(MinPositionDeltaForDirty))
	{
		Listener->Position = Position;
		Listener->bPositionSet = true;
		Listener->bDirty = true;
	}
}

FRaytraceManager::FSourceResult FRaytraceManager::GetLatestResults(uint32 SourceId) const
{
	FSourceResult Out;
	TSharedPtr<FSourceRayData> RayData = FindSourceRayData(SourceId);
	if (RayData.IsValid())
	{
		RayData->Result.TryRead(Out);   // on failure Out keeps its safe defaults
	}
	return Out;
}

FRaytraceManager::FRoomResult FRaytraceManager::GetLatestRoomResult() const
{
	FRoomResult Out;
	if (Listener.IsValid())
	{
		Listener->Result.TryRead(Out);
	}
	return Out;
}

TSharedPtr<FRaytraceManager::FSourceRayData> FRaytraceManager::FindSourceRayData(uint32 SourceId) const
{
	FReadScopeLock Lock(ResultsLock);
	const TSharedPtr<FSourceRayData>* Entry = Results.Find(SourceId);
	return Entry ? *Entry : nullptr;
}

bool FRaytraceManager::IsWorldTraceable() const
{
	return World.IsValid()
		&& World->GetPhysicsScene() != nullptr
		&& World->GetPhysicsScene()->GetSolver() != nullptr;
}

FVector FRaytraceManager::SampleBounceDirection(bool bHasHitSurface, const FVector& SegmentStart,
	const FVector& PreviousSegmentStart, const FVector& LastHitNormal, float LastHitScattering,
	FRandomStream& RndStrm)
{
	if (!bHasHitSurface)
	{
		return RndStrm.VRand();
	}

	if (RndStrm.FRand() < LastHitScattering)
	{
		return RandomCosineWeightedHemisphere(LastHitNormal, RndStrm);
	}

	const FVector IncomingDir = (SegmentStart - PreviousSegmentStart).GetSafeNormal();
	return FMath::GetReflectionVector(IncomingDir, LastHitNormal);
}

FVector FRaytraceManager::RandomCosineWeightedHemisphere(const FVector& Normal, FRandomStream& RndStrm)
{
	const float u1 = RndStrm.FRand();
	const float u2 = RndStrm.FRand();
	const float r = FMath::Sqrt(u1);
	const float Theta = 2.f * PI * u2;

	const float x = r * FMath::Cos(Theta);
	const float y = r * FMath::Sin(Theta);
	const float z = FMath::Sqrt(FMath::Max(0.f, 1.f - u1));

	const FVector Up = FMath::Abs(Normal.Z) < 0.999f ? FVector(0, 0, 1) : FVector(1, 0, 0);
	const FVector Tangent = FVector::CrossProduct(Up, Normal).GetSafeNormal();
	const FVector Bitangent = FVector::CrossProduct(Normal, Tangent);

	return (Tangent * x + Bitangent * y + Normal * z).GetSafeNormal();
}

FRaytraceManager::FSurfaceAcoustics FRaytraceManager::ResolveSurface(const FHitResult& HitResult)
{
	FSurfaceAcoustics Out;

	const UAcousticPhysicalMaterial* PhysMat = Cast<UAcousticPhysicalMaterial>(HitResult.PhysMaterial.Get());
	const UAcousticMaterialAsset* Mat = PhysMat ? PhysMat->AcousticMaterial.Get() : nullptr;

	if (Mat)
	{
		TArrayView<const float> Absorption = Mat->GetAbsorption();
		TArrayView<const float> Transmission = Mat->GetTransmission();

		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			Out.Absorption[Band] = FMath::Clamp(Absorption[Band], 0.f, 1.f);
			Out.Reflected[Band] = FMath::Clamp(1.f - Absorption[Band] - Transmission[Band], 0.f, 1.f);
		}
		Out.Scattering = Mat->GetBakedScattering();
	}
	else
	{
		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			Out.Absorption[Band] = RTA::DefaultAbsorption;
			Out.Reflected[Band] = RTA::DefaultReflection;
		}
		Out.Scattering = RTA::DefaultScattering;
	}

	return Out;
}

bool FRaytraceManager::TickAudioTrace(float /*DeltaTime*/)
{
	bool bExpected = false;
	if (!bAudioTraceRunning.compare_exchange_strong(bExpected, true))
	{
		return true;
	}

	bool bListenerDirty = false;
	bool bListenerReady = false;
	if (Listener.IsValid())
	{
		FWriteScopeLock Lock(Listener->Lock);
		bListenerReady = Listener->bPositionSet;
		bListenerDirty = Listener->bDirty;
		Listener->bDirty = false;
	}

	TArray<uint32> SourceIdsToTrace;
	if (bListenerReady)
	{
		FReadScopeLock Lock(ResultsLock);
		for (const TTuple<uint32, TSharedPtr<FSourceRayData>>& Result : Results)
		{
			FWriteScopeLock SourceLock(Result.Value->Lock);
			if ((Result.Value->bDirty || bListenerDirty)
				&& Result.Value->bEmitterPositionSet
				&& !Result.Value->bTraceInFlight)
			{
				Result.Value->bDirty = false;
				Result.Value->bTraceInFlight = true;
				SourceIdsToTrace.Add(Result.Key);
			}
		}
	}

	if (SourceIdsToTrace.Num() == 0)
	{
		bAudioTraceRunning.store(false);
		return true;
	}

	const uint32 TraceSeed = static_cast<uint32>(TraceCounter.Add(1));

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

void FRaytraceManager::RunAudioTrace(const TArray<uint32>& SourceIds, uint32 TraceSeed)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RTA_RunAudioTrace);

	ON_SCOPE_EXIT
	{
		for (uint32 SourceId : SourceIds)
		{
			if (TSharedPtr<FSourceRayData> RayData = FindSourceRayData(SourceId))
			{
				FWriteScopeLock Lock(RayData->Lock);
				RayData->bTraceInFlight = false;
			}
		}
		bAudioTraceRunning.store(false);
	};

	if (SourceIds.Num() == 0) return;

	if (!IsWorldTraceable()) return;
	if (!Listener.IsValid()) return;

	FVector ListenerPos;
	{
		FReadScopeLock Lock(Listener->Lock);
		if (!Listener->bPositionSet) return;
		ListenerPos = Listener->Position;
	}

	TArray<FValidData> ValidData;
	if (!GatherDirectData(ListenerPos, SourceIds, ValidData)) return;

	TArray<FLossAccumulator> TotalLoss;
	TotalLoss.Init(FLossAccumulator(), ValidData.Num());

	BounceRaysTrace(ListenerPos, ValidData, TraceSeed, TotalLoss);

	for (int32 SrcIdx = 0; SrcIdx < ValidData.Num(); ++SrcIdx)
	{
		TraceWriteBack(ValidData, TotalLoss, SrcIdx);
	}
}

bool FRaytraceManager::GatherDirectData(const FVector& ListenerPos, const TArray<uint32>& SourceIds,
	TArray<FValidData>& OutValidData) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RTA_DirectTraces);

	FCollisionQueryParams DirectTraceParams;
	DirectTraceParams.bReturnPhysicalMaterial = true;
	DirectTraceParams.bTraceComplex = true;

	for (uint32 SourceId : SourceIds)
	{
		TSharedPtr<FSourceRayData> RayData = FindSourceRayData(SourceId);
		if (!RayData.IsValid()) continue;

		FValidData Entry;
		Entry.SourceData = RayData;
		Entry.SourceId = SourceId;

		float MinDist, MaxDist, CutoffMin, CutoffMax;
		{
			FReadScopeLock Lock(RayData->Lock);
			Entry.EmitterPos = RayData->EmitterPosition;
			MinDist = RayData->AirAbsorptionMinDistance;
			MaxDist = FMath::Max(RayData->AirAbsorptionMaxDistance, MinDist + KINDA_SMALL_NUMBER);
			CutoffMin = RayData->AirAbsorptionCutoffAtMinDistance;
			CutoffMax = RayData->AirAbsorptionCutoffAtMaxDistance;
		}

		FHitResult DirectHit;
		const bool bBlocked = World->LineTraceSingleByChannel(
			DirectHit, ListenerPos, Entry.EmitterPos, ECC_Visibility, DirectTraceParams);

		Entry.bDirectLOS = !bBlocked;

		if (bBlocked)
		{
			const FSurfaceAcoustics Surface = ResolveSurface(DirectHit);
			const UAcousticPhysicalMaterial* PhysMat =
				Cast<UAcousticPhysicalMaterial>(DirectHit.PhysMaterial.Get());
			const UAcousticMaterialAsset* Mat = PhysMat ? PhysMat->AcousticMaterial.Get() : nullptr;

			if (Mat)
			{
				TArrayView<const float> Transmission = Mat->GetTransmission();
				for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				{
					Entry.DirectTransmissionEnergy[Band] = FMath::Clamp(Transmission[Band], 0.f, 1.f);
				}
			}
			else
			{
				for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				{
					Entry.DirectTransmissionEnergy[Band] = RTA::DefaultTransmission;
				}
			}
		}

		const float Distance = FVector::Dist(ListenerPos, Entry.EmitterPos);
		const float Alpha = FMath::Clamp((Distance - MinDist) / (MaxDist - MinDist), 0.f, 1.f);
		Entry.AirAbsorptionCutoffHz = FMath::Lerp(CutoffMin, CutoffMax, Alpha);

		OutValidData.Add(MoveTemp(Entry));
	}

	return OutValidData.Num() > 0;
}

void FRaytraceManager::BounceRaysTrace(const FVector& ListenerPos, const TArray<FValidData>& ValidData,
	uint32 TraceSeed, TArray<FLossAccumulator>& TotalLoss) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RTA_BounceRays);

	const int32 NumSources = ValidData.Num();

	// Reach far enough to find a surface that sees the furthest source, and no further.
	float FurthestSourceCm = 0.f;
	for (const FValidData& Entry : ValidData)
	{
		FurthestSourceCm = FMath::Max(FurthestSourceCm, float(FVector::Dist(ListenerPos, Entry.EmitterPos)));
	}
	const float RayLength = FMath::Clamp(FurthestSourceCm * OcclusionRayLengthHeadroom,
	                                     MinOcclusionRayLengthCm, MaxOcclusionRayLengthCm);

	FCollisionQueryParams CollisionQueryParams;
	CollisionQueryParams.bReturnPhysicalMaterial = true;
	CollisionQueryParams.bTraceComplex = true;

	FRandomStream RndStrm;
	TArray<bool> bConnected;
	bConnected.Reserve(NumSources);

	for (int32 RayNum = 0; RayNum < RayCount; ++RayNum)
	{
		bConnected.Reset();
		for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
		{
			bConnected.Add(ValidData[SrcIdx].bDirectLOS);
		}

		FVector SegmentStart = ListenerPos;
		FVector PreviousSegmentStart = ListenerPos;
		FVector LastHitNormal = FVector::ZeroVector;
		float LastHitScattering = RTA::DefaultScattering;
		bool bHasHitSurface = false;

		float RayEnergy[RTA::NumBands] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
		float PathLengthCm = 0.f;

		RndStrm.Initialize(HashCombine(TraceSeed, static_cast<uint32>(RayNum)));

		for (int32 Depth = 0; Depth < OcclusionMaxDepth; ++Depth)
		{
			const FVector RandomDir = SampleBounceDirection(
				bHasHitSurface, SegmentStart, PreviousSegmentStart, LastHitNormal, LastHitScattering, RndStrm);

			const FVector TraceEnd = SegmentStart + RandomDir * RayLength;

			FHitResult HitResult;
			World->LineTraceSingleByChannel(HitResult, SegmentStart, TraceEnd, ECC_Visibility, CollisionQueryParams);
			if (!HitResult.IsValidBlockingHit())
				break;

			PathLengthCm += HitResult.Distance;

			const FSurfaceAcoustics Surface = ResolveSurface(HitResult);
			for (int32 Band = 0; Band < RTA::NumBands; ++Band)
			{
				RayEnergy[Band] *= Surface.Reflected[Band];
			}

			PreviousSegmentStart = SegmentStart;
			SegmentStart = HitResult.Location + HitResult.Normal * SurfaceBiasCm;
			LastHitNormal = HitResult.Normal;
			LastHitScattering = Surface.Scattering;
			bHasHitSurface = true;

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RTA_BounceNeeRays);
				for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
				{
					if (bConnected[SrcIdx])
						continue;

					FHitResult NEEHitResult;
					const bool bNEEHit = World->LineTraceSingleByChannel(
						NEEHitResult, SegmentStart, ValidData[SrcIdx].EmitterPos, ECC_Visibility);

					if (!bNEEHit)
					{
						const float ShadowDistanceCm = FVector::Dist(SegmentStart, ValidData[SrcIdx].EmitterPos);
						const float TotalPathMetres = (PathLengthCm + ShadowDistanceCm) * RTA::CmToMetres;

						for (int32 Band = 0; Band < RTA::NumBands; ++Band)
						{
							const float AirAtten = FMath::Exp(
								-RTA::AirAbsorptionPerMetre[Band] * TotalPathMetres);
							const float ArrivingEnergy = FMath::Clamp(RayEnergy[Band] * AirAtten, 0.f, 1.f);
							TotalLoss[SrcIdx].Bands[Band] += (1.f - ArrivingEnergy);
						}

						bConnected[SrcIdx] = true;
					}
				}
			}
		}

		for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
		{
			if (!bConnected[SrcIdx])
			{
				for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				{
					TotalLoss[SrcIdx].Bands[Band] += 1.f;
				}
			}
		}
	}
}

void FRaytraceManager::TraceWriteBack(const TArray<FValidData>& ValidData,
	const TArray<FLossAccumulator>& TotalLoss, const int32 SrcIdx) const
{
	const TSharedPtr<FSourceRayData>& RayData = ValidData[SrcIdx].SourceData;

	FSourceResult Published;

	{
		FWriteScopeLock Lock(RayData->Lock);

		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			const float IndirectLoss = TotalLoss[SrcIdx].Bands[Band] / float(RayCount);
			const float IndirectEnergy = 1.f - IndirectLoss;
			const float DirectEnergy = ValidData[SrcIdx].DirectTransmissionEnergy[Band];

			const float FinalEnergy = DirectEnergy + (1.f - DirectEnergy) * IndirectEnergy;
			const float NewEstimate = FMath::Clamp(1.f - FinalEnergy, 0.f, 1.f);

			RayData->DirectTransmissionLoss[Band] = RayData->bHasValidEstimate
				? FMath::Lerp(RayData->DirectTransmissionLoss[Band], NewEstimate, OcclusionSmoothingAlpha)
				: NewEstimate;
		}

		RayData->DirectLowpassCutoffHz = RayData->bHasValidEstimate
			? FMath::Lerp(RayData->DirectLowpassCutoffHz, ValidData[SrcIdx].AirAbsorptionCutoffHz, OcclusionSmoothingAlpha)
			: ValidData[SrcIdx].AirAbsorptionCutoffHz;

		RayData->bHasValidEstimate = true;

		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			Published.DirectTransmissionLoss[Band] = RayData->DirectTransmissionLoss[Band];
		}
		Published.DirectLowpassCutoffHz = RayData->DirectLowpassCutoffHz;
		Published.bHasValidEstimate = true;
	}

	RayData->Result.Write(Published);

	UE_LOG(LogRTA, Verbose, TEXT("SourceId=%u Loss=[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f] CutoffHz=%.0f"),
		ValidData[SrcIdx].SourceId,
		Published.DirectTransmissionLoss[0], Published.DirectTransmissionLoss[1],
		Published.DirectTransmissionLoss[2], Published.DirectTransmissionLoss[3],
		Published.DirectTransmissionLoss[4], Published.DirectTransmissionLoss[5],
		Published.DirectLowpassCutoffHz);
}


bool FRaytraceManager::TickRoomProbe(float /*DeltaTime*/)
{
	bool bExpected = false;
	if (!bRoomProbeRunning.compare_exchange_strong(bExpected, true))
	{
		return true;
	}

	if (!Listener.IsValid())
	{
		bRoomProbeRunning.store(false);
		return true;
	}

	{
		FReadScopeLock Lock(Listener->Lock);
		if (!Listener->bPositionSet)
		{
			bRoomProbeRunning.store(false);
			return true;
		}
	}

	const uint32 TraceSeed = static_cast<uint32>(TraceCounter.Add(1));

	TWeakPtr<FRaytraceManager, ESPMode::ThreadSafe> WeakSelf = AsWeak();
	UE::Tasks::Launch(TEXT("RTA_RoomProbe"), [WeakSelf, TraceSeed]()
	{
		if (TSharedPtr<FRaytraceManager, ESPMode::ThreadSafe> Self = WeakSelf.Pin())
		{
			Self->RunRoomProbe(TraceSeed);
		}
	}, LowLevelTasks::ETaskPriority::BackgroundLow);

	return true;
}

void FRaytraceManager::RunRoomProbe(uint32 TraceSeed)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RTA_RoomProbe);

	ON_SCOPE_EXIT { bRoomProbeRunning.store(false); };

	if (!IsWorldTraceable() || !Listener.IsValid()) return;

	FVector ListenerPos;
	{
		FReadScopeLock Lock(Listener->Lock);
		if (!Listener->bPositionSet) return;
		ListenerPos = Listener->Position;
	}

	FEchogram& Fresh = Listener->FreshEchogram;
	Fresh.Reset();

	FCollisionQueryParams CollisionQueryParams;
	CollisionQueryParams.bReturnPhysicalMaterial = true;
	CollisionQueryParams.bTraceComplex = true;

	double PathLengthSumCm = 0.0;
	double AbsorptionSum[RTA::NumBands] = {};
	int32  HitCount = 0;
	int32  EscapedRays = 0;
	float  MaxHitDistanceCm = 0.f;   // diagnostic only; feeds nothing

	FRandomStream RndStrm;

	for (int32 RayNum = 0; RayNum < RayCount; ++RayNum)
	{
		FVector SegmentStart = ListenerPos;
		FVector PreviousSegmentStart = ListenerPos;
		FVector LastHitNormal = FVector::ZeroVector;
		float LastHitScattering = RTA::DefaultScattering;
		bool bHasHitSurface = false;

		float RayEnergy[RTA::NumBands] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
		float PathLengthCm = 0.f;

		RndStrm.Initialize(HashCombine(TraceSeed, static_cast<uint32>(RayNum)));

		for (int32 Depth = 0; Depth < ProbeMaxDepth; ++Depth)
		{
			const float RemainingPathCm = FEchogram::MaxPathCm - PathLengthCm;
			if (RemainingPathCm <= 0.f)
			{
				break;
			}

			const FVector RandomDir = SampleBounceDirection(
				bHasHitSurface, SegmentStart, PreviousSegmentStart, LastHitNormal, LastHitScattering, RndStrm);

			const FVector TraceEnd = SegmentStart + RandomDir * RemainingPathCm;

			FHitResult HitResult;
			World->LineTraceSingleByChannel(HitResult, SegmentStart, TraceEnd, ECC_Visibility, CollisionQueryParams);
			if (!HitResult.IsValidBlockingHit())
			{
				++EscapedRays;
				for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				{
					AbsorptionSum[Band] += 1.0;
				}
				++HitCount;
				break;
			}

			PathLengthCm += HitResult.Distance;
			PathLengthSumCm += HitResult.Distance;
			MaxHitDistanceCm = FMath::Max(MaxHitDistanceCm, HitResult.Distance);
			++HitCount;

			const FSurfaceAcoustics Surface = ResolveSurface(HitResult);
			for (int32 Band = 0; Band < RTA::NumBands; ++Band)
			{
				AbsorptionSum[Band] += Surface.Absorption[Band];
			}

			{
				const FVector HitPoint = HitResult.Location;
				const FVector ToListener = ListenerPos - HitPoint;

				const float ShadowDistanceCm = FMath::Max(ToListener.Size(), RTA::MinReceiverDistanceCm);
				const FVector DirectionToListener = ToListener / ShadowDistanceCm;

				const float CosTheta = FMath::Max(
					FVector::DotProduct(HitResult.Normal, DirectionToListener), 0.f);

				if (CosTheta > 0.f)
				{
					const FVector ShadowStart = HitPoint + HitResult.Normal * SurfaceBiasCm;

					FHitResult ShadowHit;
					const bool bShadowBlocked = World->LineTraceSingleByChannel(
						ShadowHit, ShadowStart, ListenerPos, ECC_Visibility, CollisionQueryParams);

					if (!bShadowBlocked)
					{
						const float TotalPathCm = PathLengthCm + ShadowDistanceCm;
						const float TotalPathMetres = TotalPathCm * RTA::CmToMetres;

						const int32 Bin = Fresh.BinFromPathLengthCm(TotalPathCm);

						if (Bin > 0)
						{
							const float SolidAngleFraction =
								FMath::Square(RTA::ReceiverRadiusCm) / FMath::Square(ShadowDistanceCm);
							const float GeometryTerm = Surface.Scattering * CosTheta * SolidAngleFraction;

							for (int32 Band = 0; Band < RTA::NumBands; ++Band)
							{
								const float AirAtten = FMath::Exp(
									-RTA::AirAbsorptionPerMetre[Band] * TotalPathMetres);

								Fresh.At(Band, Bin) +=
									RayEnergy[Band] * Surface.Reflected[Band] * GeometryTerm * AirAtten;
							}
						}
					}
				}
			}

			for (int32 Band = 0; Band < RTA::NumBands; ++Band)
			{
				RayEnergy[Band] *= Surface.Reflected[Band];
			}

			PreviousSegmentStart = SegmentStart;
			SegmentStart = HitResult.Location + HitResult.Normal * SurfaceBiasCm;
			LastHitNormal = HitResult.Normal;
			LastHitScattering = Surface.Scattering;
			bHasHitSurface = true;

			float PeakEnergy = 0.f;
			for (const float Energy : RayEnergy)
			{
				PeakEnergy = FMath::Max(PeakEnergy, Energy);
			}

			if (PeakEnergy < EnergyFloor)
				break;  
			if (Depth >= RouletteStartDepth)
			{
				const float q = FMath::Clamp(PeakEnergy, RouletteQMin, RouletteQMax);


				if (RndStrm.FRand() > q)
					break;

				for (float& Energy : RayEnergy)
				{
					Energy /= q;
				}
			}
		}
	}

	const float NormFactor = 1.f /
		(float(RayCount) * PI * FMath::Square(RTA::ReceiverRadiusCm));
	Fresh.Scale(NormFactor);

	FRoomResult Published;

	{
		FWriteScopeLock Lock(Listener->Lock);

		++Listener->ProbeCount;
		Listener->AccumulatedEchogram.Accumulate(Fresh, Listener->AccumulatedEchogram.GetSmoothingAlpha());
		
		TStaticArray<FDecayMetric, RTA::NumBands> DecayMetrics;
		for (int Band = 0; Band < RTA::NumBands; ++Band)
		{
			DecayMetrics[Band] = RTA::ComputeDecayMetrics(Listener->AccumulatedEchogram, Band);
		}


		if (HitCount > 0)
		{
			Listener->MeanFreePathMetres = float(PathLengthSumCm / HitCount) * RTA::CmToMetres;
			for (int32 Band = 0; Band < RTA::NumBands; ++Band)
			{
				Listener->MeanAbsorption[Band] = FMath::Clamp(
					float(AbsorptionSum[Band] / HitCount), 0.f, 0.99f);
			}
			ComputeEyringRT60(Listener->MeanFreePathMetres, Listener->MeanAbsorption, Listener->EyringRT60);
			Listener->bHasValidEstimate = true;
		}

		Listener->EscapedRayFraction = float(EscapedRays) / float(RayCount);
		Listener->MeanBounceDepth = float(HitCount) / float(RayCount);

		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			Published.EyringRT60[Band] = Listener->EyringRT60[Band];
			Published.MeanAbsorption[Band] = Listener->MeanAbsorption[Band];
			Published.MeasuredT30[Band] = DecayMetrics[Band].T30;
			Published.MeasuredT20[Band] = DecayMetrics[Band].T20;
			Published.bT30Valid[Band]   = DecayMetrics[Band].bT30Valid;
			Published.bT20Valid[Band]   = DecayMetrics[Band].bT20Valid;
		}
		{
			// 1 kHz: the densest band, so the least likely to report a spuriously late first
			// arrival from a bin that simply has not been filled yet.
			const int32 FirstBin = Listener->AccumulatedEchogram.FirstNonZeroBin(3);
			Published.FirstReflectionSeconds = (FirstBin > 0)
				? float(FirstBin) * FEchogram::BinWidthSeconds
				: 0.f;
		}

		Published.MeanFreePathMetres = Listener->MeanFreePathMetres;
		Published.EscapedRayFraction = Listener->EscapedRayFraction;
		Published.MeanBounceDepth = Listener->MeanBounceDepth;
		Published.bHasValidEstimate = Listener->bHasValidEstimate;
	}

	Listener->Result.Write(Published);

	{
		FReadScopeLock Lock(Listener->Lock);
		UE_LOG(LogRTA, Log,
			TEXT("RoomProbe: MFP=%.2fm Hits=%d MeanDepth=%.1f, MeanAbsorption=[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f], Escaped=%.0f%% MaxHit=%.0fcm "
			     "Eyring=[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f] BandTotal[1k]=%.3e LastBin[1k]=%d"),
			Published.MeanFreePathMetres, HitCount, Published.MeanBounceDepth, 
			Published.MeanAbsorption[0], Published.MeanAbsorption[1], Published.MeanAbsorption[2],
			Published.MeanAbsorption[3], Published.MeanAbsorption[4], Published.MeanAbsorption[5],
			Published.EscapedRayFraction * 100.f,
			MaxHitDistanceCm,
			Published.EyringRT60[0], Published.EyringRT60[1], Published.EyringRT60[2],
			Published.EyringRT60[3], Published.EyringRT60[4], Published.EyringRT60[5],
			Listener->AccumulatedEchogram.BandTotal(3),
			Listener->AccumulatedEchogram.LastNonZeroBin(3));
	}
}

void FRaytraceManager::ComputeEyringRT60(float MeanFreePathMetres,
	const float MeanAbsorption[RTA::NumBands], float OutRT60[RTA::NumBands])
{
	constexpr float EyringConstant = 0.161f;

	if (MeanFreePathMetres <= KINDA_SMALL_NUMBER)
	{
		for (int32 Band = 0; Band < RTA::NumBands; ++Band) OutRT60[Band] = 0.f;
		return;
	}

	const float Numerator = EyringConstant * (MeanFreePathMetres * 0.25f);

	for (int32 Band = 0; Band < RTA::NumBands; ++Band)
	{
		const float Alpha = FMath::Clamp(MeanAbsorption[Band], 0.f, 0.99f);
		const float SurfaceTerm = -FMath::Loge(1.f - Alpha);
		const float AirTerm = RTA::AirAbsorptionPerMetre[Band] * MeanFreePathMetres;
		const float Denominator = SurfaceTerm + AirTerm;

		OutRT60[Band] = (Denominator > KINDA_SMALL_NUMBER)
			? FMath::Min(Numerator / Denominator, 20.f)
			: 20.f;
	}
}


void FRaytraceManager::DumpEchogram() const
{
	if (!Listener.IsValid()) return;
	FEchogram Snapshot;
	int64 ProbeCount = 0;
	{
		FReadScopeLock Lock(Listener->Lock);
		Snapshot = Listener->AccumulatedEchogram;
		ProbeCount = Listener->ProbeCount;
	}
	
	const FDateTime Now = FDateTime::Now();
	const FString Path = FPaths::ProjectSavedDir()
		/ FString::Printf(TEXT("RTA_Echogram_%s.xlsx"), *Now.ToString(TEXT("%Y%m%d_%H%M%S")));
	const FString Note = FString::Printf(TEXT("Dumped %s, %lld probes accumulated."),
		*Now.ToString(TEXT("%Y-%m-%d %H:%M:%S")), ProbeCount);

	int32 NonFinite = 0;
	if (RTA::WriteEchogramXlsx(Path, Snapshot, Note, NonFinite))
	{
		UE_LOG(LogRTA, Log, TEXT("Echogram written to %s"), *FPaths::ConvertRelativePathToFull(Path));
	}
	else
	{
		UE_LOG(LogRTA, Warning, TEXT("Failed to write echogram to %s"), *Path);
	}
	if (NonFinite > 0)
	{
		UE_LOG(LogRTA, Warning, TEXT("Echogram contained %d non-finite values (written as 0)."), NonFinite);
	}
}