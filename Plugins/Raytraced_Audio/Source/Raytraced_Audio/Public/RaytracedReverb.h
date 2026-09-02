#pragma once
#include "IAudioExtensionPlugin.h"

class FRaytracedReverb : public IAudioReverb
{
public:
	/** Initialize the reverb plugin with the same rate and number of sources. */
	virtual void Initialize(const FAudioPluginInitializationParams InitializationParams) override;
	
	/**
	* Shuts down the audio plugin.
	*/
	virtual void Shutdown() override;

	virtual void OnDeviceShutdown(FAudioDevice* AudioDevice) override;

	/** Called when a source is assigned to a voice. */
	virtual void OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, const uint32 NumChannels, UReverbPluginSourceSettingsBase* InSettings) override;

	/** Called when a source is done playing and is released. */
	virtual void OnReleaseSource(const uint32 SourceId) override;

	/** Returns the plugin-managed effect submix instance */
	virtual FSoundEffectSubmixPtr GetEffectSubmix() override;

	virtual USoundSubmix* LoadSubmix() override
	{
		return GetSubmix();
	}

	/** Returns the plugin-managed effect submix */
	virtual USoundSubmix* GetSubmix() override;

	/** Processes audio with the given input and output data structs.*/
	virtual void ProcessSourceAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData) override;
};
