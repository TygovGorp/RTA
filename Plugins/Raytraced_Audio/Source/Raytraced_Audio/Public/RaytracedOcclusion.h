#pragma once
#include "IAudioExtensionPlugin.h"
#include "RaytraceManager.h"

struct FOcclusionFilterState
{
	float PrevLow = 0.f;
	float PrevMidLP = 0.f;
	float PrevOutput = 0.f;
};

class FRaytracedOcclusion : public IAudioOcclusion
{
public:
	FRaytracedOcclusion(const TSharedPtr<FRaytraceManager>& Manager) : RTManager(Manager)
	{}
	
	/** Initialize the occlusion plugin with the same rate and number of sources. */
	virtual void Initialize(const FAudioPluginInitializationParams InitializationParams) override;
	
	/**
	* Shuts down the audio plugin.
	*/
	virtual void Shutdown() override;

	/** Called when a source is assigned to a voice. */
	virtual void OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, const uint32 NumChannels, UOcclusionPluginSourceSettingsBase* InSettings) override;

	/** Called when a source is done playing and is released. */
	virtual void OnReleaseSource(const uint32 SourceId) override;

	/** Processes audio with the given input and output data structs.*/
	virtual void ProcessAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData) override;
	
private:
	TSharedPtr<FRaytraceManager> RTManager;
	float SampleRate = 48000.f;

	TMap<uint32, FOcclusionFilterState> FilterStates;
	FRWLock FilterStatesLock;
};