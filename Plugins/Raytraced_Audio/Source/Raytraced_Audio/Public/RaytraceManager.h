#pragma once

#include "Containers/Ticker.h"

class FRaytraceManager
{
public:
	struct FSourceRayData
	{
		FSourceRayData()
		{
			DirectTransmissionLoss	= 1.f;
			DirectLowpassCutoffHz	= 20000.f;
			AccumulatedLoss = 0.f;
			SuccessfulRayCount = 0; 
			bHasValidEstimate     = false;
			bListenerPositionSet  = false; 
			bEmitterPositionSet   = false;
			//ReverbPaths				= TArray<FBouncePathResult>();
		}
		
		FSourceRayData(const FSourceRayData& Input)
		{
			DirectTransmissionLoss = Input.DirectTransmissionLoss;
			DirectLowpassCutoffHz  = Input.DirectLowpassCutoffHz;
			AccumulatedLoss        = Input.AccumulatedLoss;
			SuccessfulRayCount     = Input.SuccessfulRayCount;
			bHasValidEstimate      = Input.bHasValidEstimate;
			bListenerPositionSet   = Input.bListenerPositionSet;
			bEmitterPositionSet    = Input.bEmitterPositionSet;
			EmitterPosition        = Input.EmitterPosition;
			ListenerPosition       = Input.ListenerPosition;
			DirtyOcclusion         = Input.DirtyOcclusion;
			DirtyReverb            = Input.DirtyReverb;
			AirAbsorptionMinDistance         = Input.AirAbsorptionMinDistance;
			AirAbsorptionMaxDistance         = Input.AirAbsorptionMaxDistance;
			AirAbsorptionCutoffAtMinDistance = Input.AirAbsorptionCutoffAtMinDistance;
			AirAbsorptionCutoffAtMaxDistance = Input.AirAbsorptionCutoffAtMaxDistance;
		}
		
		FRWLock Lock;
		float DirectTransmissionLoss = 1.f;   // 0 = fully audible, 1 = fully blocked
		float AccumulatedLoss = 0.f;
		int32 SuccessfulRayCount = 0; 
		bool bHasValidEstimate     = false;
		bool bListenerPositionSet  = false; 
		bool bEmitterPositionSet   = false;
		float DirectLowpassCutoffHz = 20000.f;
		FVector EmitterPosition = FVector(0);
		FVector ListenerPosition = FVector(0);
		//TArray<FBouncePathResult> ReverbPaths; // hit points/materials from bounce rays
		
		float AirAbsorptionMinDistance = 300.f;
		float AirAbsorptionMaxDistance = 5000.f;
		float AirAbsorptionCutoffAtMinDistance = 20000.f;
		float AirAbsorptionCutoffAtMaxDistance = 2000.f;
		
		bool DirtyOcclusion = true;
		bool DirtyReverb	= true;
	};
	FRaytraceManager();
	~FRaytraceManager();

	void RegisterSource(uint32 SourceId,
		float AirAbsorptionMinDistance = 300.f,
		float AirAbsorptionMaxDistance = 5000.f,
		float AirAbsorptionCutoffAtMinDistance = 20000.f,
		float AirAbsorptionCutoffAtMaxDistance = 2000.f);
	void UnregisterSource(uint32 SourceId);
	void UpdateEmitterPosition(uint32 SourceId, const FVector& Position); 
	void UpdateListenerPosition(uint32 SourceId, const FVector& Position); 
	FSourceRayData GetLatestResults(uint32 SourceId);
	
	void SetWorld(UWorld* WorldIn) { this->World = WorldIn; }
private:
	bool TickOcclusion(float DeltaTime);
	bool TickReverb(float DeltaTime);
	TSharedPtr<FSourceRayData> FindSourceRayData(uint32 SourceId);

	void RunOcclusionTrace(const TArray<uint32>& SourceIds);   // scheduled frequently
	void RunReverbTraces(uint32 SourceId);     // scheduled less frequently
	
	static FVector RandomCosineWeightedHemisphere(const FVector& Normal);
	
	FTSTicker::FDelegateHandle OcclusionDelegateHandle;
	FTSTicker::FDelegateHandle ReverbDelegateHandle;
	float OcclusionTickInterval = 0.033f;
	float ReverbTickInterval = 0.25f;
	int OcclusionRayCount = 1028;
	int OcclusionMaxDepth = 8;
	float MaxRayLength = 1000;
	
	UWorld* World; 
	TMap<uint32, TSharedPtr<FSourceRayData>> Results; 
	FRWLock ResultsLock;
};