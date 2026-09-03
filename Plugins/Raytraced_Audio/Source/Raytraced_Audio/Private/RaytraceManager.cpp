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
	if (Result == nullptr)
	{
		ResultsLock.ReadUnlock();
		return;
	}
	if (Result->Get()->EmitterPosition == Position)
	{
		ResultsLock.ReadUnlock();
		return;
	}
	
	Result->Get()->Lock.WriteLock();
	Result->Get()->EmitterPosition = Position;
	Result->Get()->DirtyOcclusion  = true;
	Result->Get()->DirtyReverb	   = true;
	Result->Get()->Lock.WriteUnlock();
	ResultsLock.ReadUnlock();
}

void FRaytraceManager::UpdateListenerPosition(uint32 SourceId, const FVector& Position)
{
	ResultsLock.ReadLock();
	const auto Result = this->Results.Find(SourceId);
	if (Result == nullptr)
	{
		ResultsLock.ReadUnlock();
		return;
	}
	if (Result->Get()->ListenerPosition == Position)
	{
		ResultsLock.ReadUnlock();
		return;
	}
	
	Result->Get()->Lock.WriteLock();
	Result->Get()->ListenerPosition = Position;
	Result->Get()->DirtyOcclusion  = true;
	Result->Get()->DirtyReverb	   = true;
	Result->Get()->Lock.WriteUnlock();
	ResultsLock.ReadUnlock();
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
		if (Result.Value->DirtyOcclusion)
		{
			Result.Value->Lock.WriteLock();
			Result.Value->DirtyOcclusion = false;
			Result.Value->Lock.WriteUnlock();
			SourceIdsToTrace.Add(Result.Key);
		}
	}
	ResultsLock.ReadUnlock();

	for (uint32 SourceId : SourceIdsToTrace)
	{
		RunOcclusionTrace(SourceId);
	}

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

void FRaytraceManager::RunOcclusionTrace(uint32 SourceId)
{
	ResultsLock.ReadLock();
	auto RayDataEntry = Results.Find(SourceId);
	if (!RayDataEntry)
	{
		ResultsLock.ReadUnlock();
		return;
	}
	TSharedPtr<FSourceRayData> RayData = *RayDataEntry;
	ResultsLock.ReadUnlock();

	RayData->Lock.ReadLock();
	FVector ListenerPos = RayData->ListenerPosition;
	FVector EmitterPos = RayData->EmitterPosition;
	RayData->Lock.ReadUnlock();

	FHitResult HitResult;
	bool bHit = World->LineTraceSingleByChannel(HitResult, ListenerPos, EmitterPos, ECC_Visibility);
	
	UE_LOG(LogTemp, Log, TEXT("ListenerPos: %s : EmitterPos: %s"),
	*ListenerPos.ToString(), *EmitterPos.ToString());
	
	if (World)
	{
		DrawDebugLine(World, ListenerPos, EmitterPos, bHit ? FColor::Red : FColor::Green, false, -1.0f, SDPG_Foreground, 1.5f);
	}
	

	RayData->Lock.WriteLock();
	RayData->DirectTransmissionLoss = bHit ? 1.f : 0.f;
	RayData->Lock.WriteUnlock();
}

void FRaytraceManager::RunReverbTraces(uint32 SourceId)
{
	
}
