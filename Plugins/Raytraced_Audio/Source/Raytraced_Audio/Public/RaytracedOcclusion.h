#pragma once
#include "IAudioExtensionPlugin.h"

class FRaytracedOcclusion : public IAudioOcclusion
{
public:
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
};
