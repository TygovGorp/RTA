#pragma once

#include "Containers/Ticker.h"

class FRaytraceManager : public TSharedFromThis<FRaytraceManager> 
{
public:
	struct FSourceRayData
	{
		FSourceRayData()
		{
			DirectTransmissionLoss	= {1.f};
			DirectLowpassCutoffHz	= 20000.f;
			bHasValidEstimate     = false;
			bListenerPositionSet  = false; 
			bEmitterPositionSet   = false;
			//ReverbPaths				= TArray<FBouncePathResult>();
		}
		
		FSourceRayData(const FSourceRayData& Input)
		{
			DirectTransmissionLoss	= Input.DirectTransmissionLoss;
			DirectLowpassCutoffHz	= Input.DirectLowpassCutoffHz;
			bHasValidEstimate		= Input.bHasValidEstimate;
			bListenerPositionSet	= Input.bListenerPositionSet;
			bEmitterPositionSet		= Input.bEmitterPositionSet;
			EmitterPosition			= Input.EmitterPosition;
			ListenerPosition		= Input.ListenerPosition;
			bDirty					= Input.bDirty;
			bTraceInFlight			= Input.bTraceInFlight;
			AirAbsorptionMinDistance         = Input.AirAbsorptionMinDistance;
			AirAbsorptionMaxDistance         = Input.AirAbsorptionMaxDistance;
			AirAbsorptionCutoffAtMinDistance = Input.AirAbsorptionCutoffAtMinDistance;
			AirAbsorptionCutoffAtMaxDistance = Input.AirAbsorptionCutoffAtMaxDistance;
		}
		
		FRWLock Lock;
		TStaticArray<float, 3> DirectTransmissionLoss = {1.f};   // 0 = fully audible, 1 = fully blocked
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
		
		bool bDirty = true;
		bool bTraceInFlight = false;
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
	bool TickAudioTrace(float DeltaTime);
	TSharedPtr<FSourceRayData> FindSourceRayData(uint32 SourceId);

	void RunAudioTrace(const TArray<uint32>& SourceIds);  
	
	static FVector RandomCosineWeightedHemisphere(const FVector& Normal);
	
	FTSTicker::FDelegateHandle AudioTraceDelegateHandle;
	const float AudioTraceTickInterval = 0.033f;
	const float MinPositionDeltaForDirty = 5.f;
	const int RayCount = 1028;
	const int MaxDepth = 8;
	const float MaxRayLength = 1000;
	
	TWeakObjectPtr<UWorld> World; 
	TMap<uint32, TSharedPtr<FSourceRayData>> Results; 
	FRWLock ResultsLock;
};