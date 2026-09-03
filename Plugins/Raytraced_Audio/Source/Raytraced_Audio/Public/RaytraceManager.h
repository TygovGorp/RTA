#pragma once

#include "Containers/Ticker.h"

class FRaytraceManager
{
public:
	struct FSourceRayData
	{
		FSourceRayData()
		{
			DirectTransmissionLoss	= 0;
			DirectLowpassCutoffHz	= 20000.f;
			//ReverbPaths				= TArray<FBouncePathResult>();
		}
		FSourceRayData(const FSourceRayData& Input)
		{
			DirectTransmissionLoss	= Input.DirectTransmissionLoss;
			DirectLowpassCutoffHz	= Input.DirectLowpassCutoffHz;
			//ReverbPaths				= Input.ReverbPaths;
		}
		
		FRWLock Lock;
		float DirectTransmissionLoss = 0.f;   // 0 = fully audible, 1 = fully blocked
		float DirectLowpassCutoffHz = 20000.f;
		FVector EmitterPosition = FVector(0);
		FVector ListenerPosition = FVector(0);
		//TArray<FBouncePathResult> ReverbPaths; // hit points/materials from bounce rays
		
		bool DirtyOcclusion = true;
		bool DirtyReverb	= true;
	};
	FRaytraceManager();
	~FRaytraceManager();

	void RegisterSource(uint32 SourceId);
	void UnregisterSource(uint32 SourceId);
	void UpdateEmitterPosition(uint32 SourceId, const FVector& Position); 
	void UpdateListenerPosition(uint32 SourceId, const FVector& Position); 
	FSourceRayData GetLatestResults(uint32 SourceId);
	
	void SetWorld(UWorld* WorldIn) { this->World = WorldIn; }
private:
	bool TickOcclusion(float DeltaTime);
	bool TickReverb(float DeltaTime);
	
	void RunOcclusionTrace(uint32 SourceId);   // scheduled frequently
	void RunReverbTraces(uint32 SourceId);     // scheduled less frequently
	
	FTSTicker::FDelegateHandle OcclusionDelegateHandle;
	FTSTicker::FDelegateHandle ReverbDelegateHandle;
	float OcclusionTickInterval = 0.033f;
	float ReverbTickInterval = 0.25f;
	
	UWorld* World; 
	TMap<uint32, TSharedPtr<FSourceRayData>> Results; 
	FRWLock ResultsLock;
};

