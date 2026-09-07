#include "RaytraceManager.h"

FRaytraceManager::FRaytraceManager() : World(nullptr)
{
	UE_LOG(LogTemp, Log, TEXT("RTA: Create New Manager"))
	
	OcclusionDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FRaytraceManager::TickOcclusion), 
		OcclusionTickInterval);
	
	ReverbDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FRaytraceManager::TickReverb), 
		ReverbTickInterval);
}
FRaytraceManager::~FRaytraceManager()
{
	FTSTicker::GetCoreTicker().RemoveTicker(OcclusionDelegateHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(ReverbDelegateHandle);
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

bool FRaytraceManager::TickOcclusion(float DeltaTime)
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

	RunOcclusionTrace(SourceIdsToTrace);
	return true;
}

bool FRaytraceManager::TickReverb(float DeltaTime)
{
	ResultsLock.ReadLock();
	for (TTuple<uint32, TSharedPtr<FSourceRayData>> Result : Results)
	{
		if (Result.Value.Get()->DirtyReverb)
		{
			Result.Value.Get()->Lock.WriteLock();
			Result.Value.Get()->DirtyReverb = false;
			Result.Value.Get()->Lock.WriteUnlock();
			RunReverbTraces(Result.Key);
		}
	}
	ResultsLock.ReadUnlock();
	return true;
}

TSharedPtr<FRaytraceManager::FSourceRayData> FRaytraceManager::FindSourceRayData(uint32 SourceId)
{
    FReadScopeLock Lock(ResultsLock);
    TSharedPtr<FSourceRayData>* Entry = Results.Find(SourceId);
    return Entry ? *Entry : nullptr;
}

void FRaytraceManager::RunOcclusionTrace(const TArray<uint32>& SourceIds)
{
    if (SourceIds.Num() == 0) return;

    TSharedPtr<FSourceRayData> ListenerData = FindSourceRayData(SourceIds[0]);
    if (!ListenerData) return;

    FVector ListenerPos;
    {
        FReadScopeLock Lock(ListenerData->Lock);
        ListenerPos = ListenerData->ListenerPosition;
    }
	
    TArray<TSharedPtr<FSourceRayData>> ValidSourceData;
    TArray<uint32> ValidSourceIds;
    TArray<FVector> EmitterPositions;
    TArray<bool> bDirectLOS;
    TArray<float> AirAbsorptionCutoffHz;
    float BatchMaxDistance = 0.f;

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
        const bool bBlocked = World->LineTraceSingleByChannel(DirectHit, ListenerPos, EmitterPosition, ECC_Visibility);

        const float Distance = FVector::Dist(ListenerPos, EmitterPosition);

        // Air absorption: purely distance-driven, independent of occlusion state.
        // Mirrors native Sound Attenuation's Air Absorption semantics: no effect below
        // MinDistance, full effect (CutoffAtMaxDistance) at/beyond MaxDistance, lerped
        // between. Per-source, read from this source's registered settings.
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
        AirAbsorptionCutoffHz.Add(CutoffHz);
        BatchMaxDistance = FMath::Max(BatchMaxDistance, Distance);
    }

    const int32 NumSources = ValidSourceData.Num();
    if (NumSources == 0) return;

    const float DynamicMaxRayLength = FMath::Max(MaxRayLength, BatchMaxDistance * 5.5f);
	
    TArray<float> TotalLoss;
    TotalLoss.Init(0.f, NumSources);

    for (int32 RayNum = 0; RayNum < OcclusionRayCount; ++RayNum)
    {
        TArray<bool> bConnected = bDirectLOS; // direct-LOS sources start "connected", skip them below

        FVector CurrentPos = ListenerPos;
        FVector LastHitNormal = FVector::ZeroVector;
        bool bHasHitSurface = false;

        for (int32 Depth = 0; Depth < OcclusionMaxDepth; ++Depth)
        {
            const FVector RandomDir = bHasHitSurface
                ? RandomCosineWeightedHemisphere(LastHitNormal)
                : FMath::VRand();
            const FVector TraceEnd = CurrentPos + RandomDir * DynamicMaxRayLength;

            FHitResult HitResult;
            World->LineTraceSingleByChannel(HitResult, CurrentPos, TraceEnd, ECC_Visibility);
            if (!HitResult.IsValidBlockingHit())
                break;

            constexpr float SurfaceBias = 1.f;
            CurrentPos = HitResult.Location + HitResult.Normal * SurfaceBias;
            LastHitNormal = HitResult.Normal;
            bHasHitSurface = true;

            // Next Event Estimation
            for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
            {
                if (bConnected[SrcIdx])
                    continue;

                FHitResult NEEHitResult;
                const bool bNEEHit = World->LineTraceSingleByChannel(
                    NEEHitResult, CurrentPos, EmitterPositions[SrcIdx], ECC_Visibility);

                if (!bNEEHit)
                {
                    TotalLoss[SrcIdx] += 1.f - (1.f / float(Depth + 1));
                    bConnected[SrcIdx] = true;
                }
            }
        }

        for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
        {
            if (!bConnected[SrcIdx])
            {
                TotalLoss[SrcIdx] += 1.f; // never reached this source on this ray: full loss
            }
        }
    }

    constexpr float SmoothingAlpha = 0.15f; // tune: lower = smoother/slower, higher = snappier/more jitter
    for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
    {
        const float NewEstimate = TotalLoss[SrcIdx] / float(OcclusionRayCount);
        TSharedPtr<FSourceRayData>& RayData = ValidSourceData[SrcIdx];

        FWriteScopeLock Lock(RayData->Lock);
        if (!RayData->bHasValidEstimate)
        {
            RayData->DirectTransmissionLoss = NewEstimate;
            RayData->DirectLowpassCutoffHz = AirAbsorptionCutoffHz[SrcIdx];
            RayData->bHasValidEstimate = true;
        }
        else
        {
            RayData->DirectTransmissionLoss = FMath::Lerp(RayData->DirectTransmissionLoss, NewEstimate, SmoothingAlpha);
            RayData->DirectLowpassCutoffHz = FMath::Lerp(RayData->DirectLowpassCutoffHz, AirAbsorptionCutoffHz[SrcIdx], SmoothingAlpha);
        }

        UE_LOG(LogTemp, Log, TEXT("RTA: SourceId=%u Loss=%.2f CutoffHz=%.0f"), ValidSourceIds[SrcIdx], RayData->DirectTransmissionLoss, RayData->DirectLowpassCutoffHz);
    }
}

void FRaytraceManager::RunReverbTraces(uint32 SourceId)
{
	
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