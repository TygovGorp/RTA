#pragma once

#include "IAudioExtensionPlugin.h"

class FRaytracedSpatialization : public IAudioSpatialization
{
public:
	/**
	* Shuts down the audio plugin.
	*/
	virtual void Shutdown() override;

	virtual void OnDeviceShutdown(FAudioDevice* AudioDevice) override;

	/** DEPRECATED: sets the spatialization effect parameters. */
	virtual void SetSpatializationParameters(uint32 SourceId, const FSpatializationParams& Params) override;

	/** DEPRECATED: Gets the spatialization effect parameters. */
	virtual void GetSpatializationParameters(uint32 SourceId, FSpatializationParams& OutParams) override;
	
	/** DEPRECATED: Initializes the spatialization effect with the given buffer length. */
	virtual void InitializeSpatializationEffect(uint32 BufferLength) override;

	/** DEPRECATED: Uses the given HRTF algorithm to spatialize a mono audio stream. */
	virtual void ProcessSpatializationForVoice(uint32 SourceId, float* InSamples, float* OutSamples, const FVector& Position) override;

	/** DEPRECATED: Uses the given HRTF algorithm to spatialize a mono audio stream, assumes the parameters have already been set before processing. */
	virtual void ProcessSpatializationForVoice(uint32 SourceId, float* InSamples, float* OutSamples) override;

	/** Called when a source is assigned to a voice. */
	virtual void OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, USpatializationPluginSourceSettingsBase* InSettings) override;

	virtual void OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, const uint32 NumChannels, USpatializationPluginSourceSettingsBase* InSettings) override
	{
		OnInitSource(SourceId, AudioComponentUserId, InSettings);
	}

	/** Called when a source is done playing and is released. */
	virtual void OnReleaseSource(const uint32 SourceId) override;

	/** Processes audio with the given input and output data structs.*/
	virtual void ProcessAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData) override;

	/** Called when all sources have finished processing. */
	virtual void OnAllSourcesProcessed() override;

	/** Returns whether or not the spatialization effect has been initialized */
	virtual bool IsSpatializationEffectInitialized() const override;

	/** Initializes the spatialization plugin with the given buffer length. */
	virtual void Initialize(const FAudioPluginInitializationParams InitializationParams) override;

	/** Creates an audio spatialization effect. */
	virtual bool CreateSpatializationEffect(uint32 SourceId) override;

	/**	Returns the spatialization effect for the given voice id. */
	virtual void* GetSpatializationEffect(uint32 SourceId) override;
};
