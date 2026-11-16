#pragma once
#include "IAudioExtensionPlugin.h"
#include "RaytraceManager.h"
#include "CoreMinimal.h"

struct FHRIRMeasurement
{
	FVector3f Direction = FVector3f::ZeroVector; // unit vector, Unreal axes (X forward, Y right, Z up)
	float Azimuth = 0.f;
	float Elevation = 0.f;

	TArray<float> LeftSamples;
	TArray<float> RightSamples; 
};

struct FRTAHRIRSet
{
	bool Load(const FString& Path);

	bool IsValid() const { return Measurements.Num() > 0 && Length > 0; }
	
	int32 FindNearest(const FVector3f& Dir) const
	{
		if (Measurements.Num() == 0 || Dir.IsNearlyZero())
		{
			return INDEX_NONE;
		}

		int32 Best = 0;
		float BestDot = -TNumericLimits<float>::Max();
		for (int32 i = 0; i < Measurements.Num(); ++i)
		{
			const float Dot = FVector3f::DotProduct(Dir, Measurements[i].Direction);
			if (Dot > BestDot)
			{
				BestDot = Dot;
				Best = i;
			}
		}
		return Best;
	}

	int32 SampleRate = 0;
	int32 Length = 0;

	TArray<FHRIRMeasurement> Measurements;

private:
	void Reset();
};

namespace RTA::HeadModel
{
	constexpr int32 DelaySize      = 64;        // power of two, > max ITD (~33 samples) + 1
	constexpr float HeadRadius     = 0.09f;     // metres, matches the RIEC data
	constexpr float SpeedOfSound   = 343.f;     // metres per second
	constexpr float AlphaMin       = 0.1f;
	constexpr float HeadModelGain  = 0.7071f;   // match equal-power panning's centre level

	struct FShadowFilterState { float X1 = 0.f; float Y1 = 0.f; };

	struct FState
	{
		float Buffer[DelaySize] = {};           // fixed size: no heap, reset by assignment
		int32 WritePos = 0;
		float PrevDelayL = 0.f;
		float PrevDelayR = 0.f;
		FShadowFilterState ShadowL, ShadowR;
	};
}

class FRaytracedSpatialization : public IAudioSpatialization
{
public:
	FRaytracedSpatialization(const TSharedPtr<FRaytraceManager>& Manager) : RTManager(Manager)
	{}
	
	/** Initializes the spatialization plugin with the given buffer length. */
	virtual void Initialize(const FAudioPluginInitializationParams InitializationParams) override;

	/** Creates an audio spatialization effect. */
	virtual bool CreateSpatializationEffect(uint32 SourceId) override;
	
	/** Called when a source is assigned to a voice. */
	virtual void OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, USpatializationPluginSourceSettingsBase* InSettings) override
	{
		OnInitSource(SourceId, AudioComponentUserId, 1, InSettings);
	}

	virtual void OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, const uint32 NumChannels, USpatializationPluginSourceSettingsBase* InSettings) override;

	/** Called when a source is done playing and is released. */
	virtual void OnReleaseSource(const uint32 SourceId) override;

	/** Processes audio with the given input and output data structs.*/
	virtual void ProcessAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData) override;

	/** Called when all sources have finished processing. */
	virtual void OnAllSourcesProcessed() override;
	
	/** Shuts down the audio plugin. */
	virtual void Shutdown() override;

	virtual void OnDeviceShutdown(FAudioDevice* AudioDevice) override;

	/**	Returns the spatialization effect for the given voice id. */
	virtual void* GetSpatializationEffect(uint32 SourceId) override;
	
	/** Returns whether or not the spatialization effect has been initialized */
	virtual bool IsSpatializationEffectInitialized() const override;

	TSharedPtr<FRaytraceManager> RTManager;
private:
	void ProcessHeadModel(RTA::HeadModel::FState& State, const float* In, float* OutInterleaved,
						  int32 NumFrames, const FVector3f& Dir) const;

	TArray<RTA::HeadModel::FState> HeadModelStates;   // indexed by SourceId, sized in Initialize
	
	bool bInitialized = false;
	
	int32 SampleRate = 0;
	int32 BufferLength = 0;
	int32 NumSources = 0;
	
	struct CSourceState
	{
		bool temp;
	};
	 
	TMap<int32, CSourceState> PerSourceStates;
	FRTAHRIRSet HRIRSet;
};
