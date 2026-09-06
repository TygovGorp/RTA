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

void FRaytraceManager::RegisterSource(uint32 SourceId)
{
	
	this->ResultsLock.WriteLock();
	if (Results.Find(SourceId) != nullptr) UE_LOG(LogTemp, Warning, TEXT("RTA: SourceId already present in Results Map"));
	this->Results.Add(SourceId, MakeShared<FSourceRayData>());
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

void FRaytraceManager::RunOcclusionTrace(const TArray<uint32>& SourceIds)
{	
    if (SourceIds.Num() == 0) return;

    FVector ListenerPos;
    {
        ResultsLock.ReadLock();
        auto RayDataEntry = Results.Find(SourceIds[0]);
        if (!RayDataEntry) { ResultsLock.ReadUnlock(); return; }
        TSharedPtr<FSourceRayData> RayData = *RayDataEntry;
        ResultsLock.ReadUnlock();

        RayData->Lock.ReadLock();
        ListenerPos = RayData->ListenerPosition;
        RayData->Lock.ReadUnlock();
    }
	
    TArray<float> LocalAccumulatedLoss;
    LocalAccumulatedLoss.SetNumZeroed(SourceIds.Num());
    TArray<int32> LocalSuccessCount;
    LocalSuccessCount.SetNumZeroed(SourceIds.Num());

    for (int RayNum = 0; RayNum < OcclusionRayCount; ++RayNum)
    {
        FVector startPos = ListenerPos;
        bool bHasHitSurface = false;
        FVector LastHitNormal = FVector::ZeroVector;
    	
        TArray<bool> bConnected;
        bConnected.Init(false, SourceIds.Num());

        for (int Depth = 0; Depth < OcclusionMaxDepth; ++Depth)
        {
            FVector SegmentStart = startPos;

            FVector RandomDir = bHasHitSurface
                ? RandomCosineWeightedHemisphere(LastHitNormal)
                : FMath::VRand();

            FVector TraceEnd = SegmentStart + RandomDir * MaxRayLength;

            FHitResult HitResult;
            bool bHit = World->LineTraceSingleByChannel(HitResult, SegmentStart, TraceEnd, ECC_Visibility);
            FVector SegmentEnd = bHit ? HitResult.Location : TraceEnd;

            if (!HitResult.IsValidBlockingHit())
                break;

            constexpr float SurfaceBias = 1.f;
            startPos = HitResult.Location + HitResult.Normal * SurfaceBias;
            LastHitNormal = HitResult.Normal;
            bHasHitSurface = true;

            // Next Event Estimation
            for (int32 SrcIdx = 0; SrcIdx < SourceIds.Num(); ++SrcIdx)
            {
                if (bConnected[SrcIdx])
                    continue;

                uint32 SourceId = SourceIds[SrcIdx];
                ResultsLock.ReadLock();
                auto RayDataEntry = Results.Find(SourceId);
                if (!RayDataEntry) { ResultsLock.ReadUnlock(); continue; }
                TSharedPtr<FSourceRayData> RayData = *RayDataEntry;
                ResultsLock.ReadUnlock();

                RayData->Lock.ReadLock();
                FVector EmitterPosition = RayData->EmitterPosition;
                RayData->Lock.ReadUnlock();

                FHitResult NEEHitResult;
                bool bNEEHit = World->LineTraceSingleByChannel(NEEHitResult, startPos, EmitterPosition, ECC_Visibility);

                if (!bNEEHit)
                {
                    float RayLoss = 1.f - (1.f / float(Depth + 1));
                    LocalAccumulatedLoss[SrcIdx] += RayLoss;
                    LocalSuccessCount[SrcIdx] += 1;
                    bConnected[SrcIdx] = true;
                }
            }
        }
    }
	
    constexpr float SmoothingAlpha = 0.15f; // tune: lower = smoother/slower, higher = snappier/more jitter
    for (int32 SrcIdx = 0; SrcIdx < SourceIds.Num(); ++SrcIdx)
    {
        ResultsLock.ReadLock();
        auto RayDataEntry = Results.Find(SourceIds[SrcIdx]);
        if (!RayDataEntry) { ResultsLock.ReadUnlock(); continue; }
        TSharedPtr<FSourceRayData> RayData = *RayDataEntry;
        ResultsLock.ReadUnlock();

        // Rays that never connected count as full loss (1.0) in the average.
        float TotalLoss = LocalAccumulatedLoss[SrcIdx] + (OcclusionRayCount - LocalSuccessCount[SrcIdx]) * 1.f;
        float NewEstimate = TotalLoss / float(OcclusionRayCount);

    	RayData->Lock.WriteLock();
    	if (!RayData->bHasValidEstimate)
    	{
    		RayData->DirectTransmissionLoss = NewEstimate;
    		RayData->bHasValidEstimate = true;
    	}
    	else
    	{
    		float PreviousLoss = RayData->DirectTransmissionLoss;
    		RayData->DirectTransmissionLoss = FMath::Lerp(PreviousLoss, NewEstimate, SmoothingAlpha);
    	}
    	
    	UE_LOG(LogTemp, Log, TEXT("RTA: SourceId=%u Loss=%.2f"), SourceIds[SrcIdx], RayData->DirectTransmissionLoss);
    	
    	RayData->Lock.WriteUnlock();
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
