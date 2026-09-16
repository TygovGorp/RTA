#include "RaytraceManager.h"

#include "AcousticMaterialAsset.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

FRaytraceManager::FRaytraceManager() : World(nullptr)
{
	UE_LOG(LogTemp, Log, TEXT("RTA: Create New Manager"))
	
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
	if (Results.Find(SourceId) != nullptr) UE_LOG(LogTemp, Warning, TEXT("RTA: SourceId already present in Results Map"));
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
	bool bChanged = bIsFirstSet || RayData->EmitterPosition != Position;
	if (bChanged)
	{
		RayData->EmitterPosition = Position;
		RayData->bEmitterPositionSet = true;
		RayData->DirtyOcclusion = true;
		RayData->DirtyReverb = true;
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
	bool bChanged = bIsFirstSet || RayData->ListenerPosition != Position;
	if (bChanged)
	{
		RayData->ListenerPosition = Position;
		RayData->bListenerPositionSet = true;
		RayData->DirtyOcclusion = true;
		RayData->DirtyReverb = true;
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
		if (Result.Value->DirtyOcclusion 
			&& Result.Value->bListenerPositionSet 
			&& Result.Value->bEmitterPositionSet)
		{
			Result.Value->Lock.WriteLock();
			Result.Value->DirtyOcclusion = false;
			Result.Value->Lock.WriteUnlock();
			SourceIdsToTrace.Add(Result.Key);
		}
	}
	ResultsLock.ReadUnlock();

	RunAudioTrace(SourceIdsToTrace);
	return true;
}


TSharedPtr<FRaytraceManager::FSourceRayData> FRaytraceManager::FindSourceRayData(uint32 SourceId)
{
    FReadScopeLock Lock(ResultsLock);
    TSharedPtr<FSourceRayData>* Entry = Results.Find(SourceId);
    return Entry ? *Entry : nullptr;
}

void FRaytraceManager::RunAudioTrace(const TArray<uint32>& SourceIds)
{
    if (SourceIds.Num() == 0) return;

    TSharedPtr<FSourceRayData> ListenerData = FindSourceRayData(SourceIds[0]);
    if (!ListenerData) return;

    FVector ListenerPos;
    {
        FReadScopeLock Lock(ListenerData->Lock);
        ListenerPos = ListenerData->ListenerPosition;
    }

    struct FBandEnergy { float Bands[3] = { 1.f, 1.f, 1.f }; };

    TArray<TSharedPtr<FSourceRayData>> ValidSourceData;
    TArray<uint32> ValidSourceIds;
    TArray<FVector> EmitterPositions;
    TArray<bool> bDirectLOS;
    TArray<FBandEnergy> DirectTransmissionEnergy;
    TArray<float> AirAbsorptionCutoffHz;
    float BatchMaxDistance = 0.f;
	
    FCollisionQueryParams DirectTraceParams;
    DirectTraceParams.bReturnPhysicalMaterial = true;
	DirectTraceParams.bTraceComplex = true; 

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
    	if (!World.IsValid()) return;
    	if (!World->GetPhysicsScene() || World->GetPhysicsScene()->GetSolver() == nullptr) return;
        const bool bBlocked = World->LineTraceSingleByChannel(DirectHit, ListenerPos, EmitterPosition, ECC_Visibility, DirectTraceParams);

        FBandEnergy DirectEnergy; // defaults to {1,1,1} fully audible, unblocked case

        if (bBlocked)
        {
            if (const UAcousticPhysicalMaterial* AcousticPhysMat = Cast<UAcousticPhysicalMaterial>(DirectHit.PhysMaterial.Get()))
            {
                if (const UAcousticMaterialAsset* Mat = AcousticPhysMat->AcousticMaterial.Get())
                {
                    TArrayView<const float> Transmission = Mat->GetTransmission();
                    for (int32 Band = 0; Band < 3; ++Band)
                    {
                        DirectEnergy.Bands[Band] = Transmission[Band];
                    }
                }
                else
                {
                    DirectEnergy = FBandEnergy{ {0.f, 0.f, 0.f} }; // tagged but no asset assigned: treat as fully opaque
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

        ValidSourceData.Add(RayData);
        ValidSourceIds.Add(SourceId);
        EmitterPositions.Add(EmitterPosition);
        bDirectLOS.Add(!bBlocked);
        DirectTransmissionEnergy.Add(DirectEnergy);
        AirAbsorptionCutoffHz.Add(CutoffHz);
        BatchMaxDistance = FMath::Max(BatchMaxDistance, Distance);
    }

    const int32 NumSources = ValidSourceData.Num();
    if (NumSources == 0) return;

    const float DynamicMaxRayLength = FMath::Max(MaxRayLength, BatchMaxDistance * 5.5f);

    struct FLossAccumulator { float Bands[3] = { 0.f, 0.f, 0.f }; };
    TArray<FLossAccumulator> TotalLoss;
    TotalLoss.Init(FLossAccumulator(), NumSources);

    FCollisionQueryParams CollisionQueryParams;
    CollisionQueryParams.bReturnPhysicalMaterial = true;

    for (int32 RayNum = 0; RayNum < RayCount; ++RayNum)
    {
        TArray<bool> bConnected = bDirectLOS;

        FVector SegmentStart = ListenerPos;
        FVector PreviousSegmentStart = ListenerPos;
        FVector LastHitNormal = FVector::ZeroVector;
        float LastHitScattering = 0.5f;
        bool bHasHitSurface = false;

        float RayEnergy[3] = { 1.f, 1.f, 1.f };

        for (int32 Depth = 0; Depth < MaxDepth; ++Depth)
        {
            FVector RandomDir;

            if (!bHasHitSurface)
            {
                RandomDir = FMath::VRand();
            }
            else
            {
                const float ScatterRoll = FMath::FRand();
                if (ScatterRoll < LastHitScattering)
                {
                    RandomDir = RandomCosineWeightedHemisphere(LastHitNormal);
                }
                else
                {
                    const FVector IncomingDir = (SegmentStart - PreviousSegmentStart).GetSafeNormal();
                    RandomDir = FMath::GetReflectionVector(IncomingDir, LastHitNormal);
                }
            }

            const FVector TraceEnd = SegmentStart + RandomDir * DynamicMaxRayLength;

            FHitResult HitResult;
            World->LineTraceSingleByChannel(HitResult, SegmentStart, TraceEnd, ECC_Visibility, CollisionQueryParams);
            if (!HitResult.IsValidBlockingHit())
                break;

            float HitScattering = 0.5f;

            if (const UAcousticPhysicalMaterial* AcousticPhysMat = Cast<UAcousticPhysicalMaterial>(HitResult.PhysMaterial.Get()))
            {
                if (const UAcousticMaterialAsset* Mat = AcousticPhysMat->AcousticMaterial.Get())
                {
                    TArrayView<const float> Absorption = Mat->GetAbsorption();
                    TArrayView<const float> Transmission = Mat->GetTransmission();

                    for (int32 Band = 0; Band < 3; ++Band)
                    {
                        const float Reflected = 1.f - Absorption[Band] - Transmission[Band];
                        RayEnergy[Band] *= FMath::Clamp(Reflected, 0.f, 1.f);
                    }

                    HitScattering = Mat->GetBakedScattering();
                }
            }

            constexpr float SurfaceBias = 1.f;
            PreviousSegmentStart = SegmentStart;
            SegmentStart = HitResult.Location + HitResult.Normal * SurfaceBias;
            LastHitNormal = HitResult.Normal;
            LastHitScattering = HitScattering;
            bHasHitSurface = true;

            for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
            {
                if (bConnected[SrcIdx])
                    continue;

                FHitResult NEEHitResult;
                const bool bNEEHit = World->LineTraceSingleByChannel(
                    NEEHitResult, SegmentStart, EmitterPositions[SrcIdx], ECC_Visibility);

                if (!bNEEHit)
                {
                    for (int32 Band = 0; Band < 3; ++Band)
                    {
                        TotalLoss[SrcIdx].Bands[Band] += (1.f - RayEnergy[Band]);
                    }
                    bConnected[SrcIdx] = true;
                }
            }
        }

        for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
        {
            if (!bConnected[SrcIdx])
            {
                for (int32 Band = 0; Band < 3; ++Band)
                {
                    TotalLoss[SrcIdx].Bands[Band] += 1.f;
                }
            }
        }
    }

    constexpr float SmoothingAlpha = 0.15f;
    for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
    {
        TSharedPtr<FSourceRayData>& RayData = ValidSourceData[SrcIdx];

        FWriteScopeLock Lock(RayData->Lock);
        for (int32 Band = 0; Band < 3; ++Band)
        {
            const float IndirectLoss = TotalLoss[SrcIdx].Bands[Band] / float(RayCount);
            const float IndirectEnergy = 1.f - IndirectLoss;
            const float DirectEnergy = DirectTransmissionEnergy[SrcIdx].Bands[Band];
        	
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
            RayData->DirectLowpassCutoffHz = AirAbsorptionCutoffHz[SrcIdx];
            RayData->bHasValidEstimate = true;
        }
        else
        {
            RayData->DirectLowpassCutoffHz = FMath::Lerp(RayData->DirectLowpassCutoffHz, AirAbsorptionCutoffHz[SrcIdx], SmoothingAlpha);
        }

        UE_LOG(LogTemp, Log, TEXT("RTA: SourceId=%u Loss=[%.2f,%.2f,%.2f] CutoffHz=%.0f"),
            ValidSourceIds[SrcIdx],
            RayData->DirectTransmissionLoss[0], RayData->DirectTransmissionLoss[1], RayData->DirectTransmissionLoss[2],
            RayData->DirectLowpassCutoffHz);
    }
}

FVector FRaytraceManager::RandomCosineWeightedHemisphere(const FVector& Normal)
{
	float u1 = FMath::FRand();
	float u2 = FMath::FRand();
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