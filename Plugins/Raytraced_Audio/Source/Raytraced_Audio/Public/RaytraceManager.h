#pragma once

#include "RTAAcousticBands.h"
#include "Echogram.h"
#include "RTASeqLock.h"
#include "Containers/Ticker.h"
#include "Engine/HitResult.h"
#include "Math/RandomStream.h"
#include <atomic>

class UAcousticMaterialAsset;
class UWorld;

class FRaytraceManager : public TSharedFromThis<FRaytraceManager>
{
public:
	struct FSourceResult
	{
		float DirectTransmissionLoss[RTA::NumBands] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f }; // 0 = audible, 1 = blocked
		float DirectLowpassCutoffHz = 20000.f;
		bool  bHasValidEstimate = false;
	};

	struct FRoomResult
	{
		float FirstReflectionSeconds = 0.f;

		float MeasuredT30[RTA::NumBands] = {};
		float MeasuredT20[RTA::NumBands] = {};
		bool  bT30Valid[RTA::NumBands] = {};
		bool  bT20Valid[RTA::NumBands] = {};

		float EyringRT60[RTA::NumBands] = {};
		float MeanAbsorption[RTA::NumBands] = {};
		float MeanFreePathMetres = 0.f;
		float EscapedRayFraction = 0.f;
		float MeanBounceDepth = 0.f;
		bool  bHasValidEstimate = false;
	};

	struct FSourceRayData
	{
		FSourceRayData() = default;

		FRWLock Lock;
		
		FVector EmitterPosition = FVector::ZeroVector;
		bool bEmitterPositionSet = false;
		bool bDirty = true;
		bool bTraceInFlight = false;

		float DirectTransmissionLoss[RTA::NumBands] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
		float DirectLowpassCutoffHz = 20000.f;
		bool  bHasValidEstimate = false;

		float AirAbsorptionMinDistance = 300.f;
		float AirAbsorptionMaxDistance = 5000.f;
		float AirAbsorptionCutoffAtMinDistance = 20000.f;
		float AirAbsorptionCutoffAtMaxDistance = 2000.f;

		TRTASeqLock<FSourceResult> Result;
	};

	struct FListenerData
	{
		FRWLock Lock;

		FVector Position = FVector::ZeroVector;
		bool bPositionSet = false;
		bool bDirty = true;

		FEchogram FreshEchogram;
		FEchogram AccumulatedEchogram;

		int32 ProbeCount = 0;

		float EyringRT60[RTA::NumBands] = {};
		float MeanAbsorption[RTA::NumBands] = {};
		float MeanFreePathMetres = 0.f;
		float EscapedRayFraction = 0.f;
		float MeanBounceDepth = 0.f;
		bool  bHasValidEstimate = false;

		TRTASeqLock<FRoomResult> Result;
	};

	struct FValidData
	{
		TSharedPtr<FSourceRayData> SourceData;
		uint32 SourceId = 0;
		FVector EmitterPos = FVector::ZeroVector;
		bool bDirectLOS = false;
		float DirectTransmissionEnergy[RTA::NumBands] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
		float AirAbsorptionCutoffHz = 20000.f;
	};

	struct FLossAccumulator { float Bands[RTA::NumBands] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f }; };

	struct FSurfaceAcoustics
	{
		float Reflected[RTA::NumBands] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
		float Absorption[RTA::NumBands] = {};
		float Scattering = RTA::DefaultScattering;
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

	void UpdateListenerPosition(const FVector& Position);

	FSourceResult GetLatestResults(uint32 SourceId) const;
	FRoomResult   GetLatestRoomResult() const;

	void SetWorld(UWorld* WorldIn) { this->World = WorldIn; }

	void DumpEchogram() const;

private:
	bool TickAudioTrace(float DeltaTime);
	void RunAudioTrace(const TArray<uint32>& SourceIds, uint32 TraceSeed);
	bool GatherDirectData(const FVector& ListenerPos, const TArray<uint32>& SourceIds,
	                      TArray<FValidData>& OutValidData) const;
	void BounceRaysTrace(const FVector& ListenerPos, const TArray<FValidData>& ValidData,
	                     uint32 TraceSeed, TArray<FLossAccumulator>& TotalLoss) const;
	void TraceWriteBack(const TArray<FValidData>& ValidData,
	                    const TArray<FLossAccumulator>& TotalLoss, int32 SrcIdx) const;

	bool TickRoomProbe(float DeltaTime);
	void RunRoomProbe(uint32 TraceSeed);
	static void ComputeEyringRT60(float MeanFreePathMetres,
	                              const float MeanAbsorption[RTA::NumBands],
	                              float OutRT60[RTA::NumBands]);

	TSharedPtr<FSourceRayData> FindSourceRayData(uint32 SourceId) const;
	bool IsWorldTraceable() const;
	static FVector SampleBounceDirection(bool bHasHitSurface, const FVector& SegmentStart,
	                                     const FVector& PreviousSegmentStart,
	                                     const FVector& LastHitNormal, float LastHitScattering,
	                                     FRandomStream& RndStrm);
	static FVector RandomCosineWeightedHemisphere(const FVector& Normal, FRandomStream& RndStrm);
	static FSurfaceAcoustics ResolveSurface(const FHitResult& HitResult);

	FTSTicker::FDelegateHandle AudioTraceDelegateHandle;
	FTSTicker::FDelegateHandle RoomProbeDelegateHandle;

	static constexpr float AudioTraceTickInterval = 0.033f;
	static constexpr float RoomProbeTickInterval  = 0.25f;
	static constexpr float MinPositionDeltaForDirty = 5.f;
	static constexpr float ClusterSizeCm = 300.f;   // 3 m

	static constexpr int32 RayCount = 1028;
	static constexpr int32 OcclusionMaxDepth = 8;
	static constexpr int32 ProbeMaxDepth = 64;
	static constexpr int32 RouletteStartDepth = 10;
	static constexpr float RouletteQMin = 0.05f;
	static constexpr float RouletteQMax = 0.99f;
	static constexpr float EnergyFloor = 1.0e-6f;

	static constexpr float SurfaceBiasCm = 1.f;
	static constexpr float OcclusionSmoothingAlpha = 0.15f;
	
	static constexpr float OcclusionRayLengthHeadroom = 2.0f;
	static constexpr float MinOcclusionRayLengthCm = 1000.f;    //  10 m
	static constexpr float MaxOcclusionRayLengthCm = 20000.f;   // 200 m

	std::atomic<bool> bAudioTraceRunning{ false };
	std::atomic<bool> bRoomProbeRunning{ false };

	FThreadSafeCounter TraceCounter;

	TWeakObjectPtr<UWorld> World;
	TMap<uint32, TSharedPtr<FSourceRayData>> Results;
	mutable FRWLock ResultsLock;

	TSharedPtr<FListenerData> Listener;
};