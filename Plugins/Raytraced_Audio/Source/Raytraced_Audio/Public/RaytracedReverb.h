#pragma once
#include "IAudioExtensionPlugin.h"
#include "RaytraceManager.h"
#include "RTAReverbSubmix.h"

class USoundSubmix;

class FRaytracedReverb : public IAudioReverb
{
public:
	FRaytracedReverb(const TSharedPtr<FRaytraceManager>& Manager): RTManager(Manager)
	{}

	/** Initialize the reverb plugin with the same rate and number of sources. */
	virtual void Initialize(const FAudioPluginInitializationParams InitializationParams) override;

	/**
	* Shuts down the audio plugin.
	*/
	virtual void Shutdown() override;

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

private:
	struct FReverbSourceState
	{
		bool   bEnableReverb = true;
		float  SendGain = 1.f;
		uint32 NumChannels = 0;

		// Previous buffer's send, so the gain can ramp instead of stepping.
		float  PrevSend = -1.f;
	};

	/** Reads the room probe and pushes decay into the submix effect. Audio render thread. */
	void UpdateRoomDecay();

	TSharedPtr<FRaytraceManager> RTManager;
	
	TObjectPtr<USoundSubmix> ReverbSubmix = nullptr;
	TObjectPtr<URTAReverbSubmixPreset> ReverbPreset = nullptr;

	FSoundEffectSubmixPtr SubmixEffect = nullptr;

	// Audio thread only: OnInitSource, OnReleaseSource and ProcessSourceAudio all run there.
	TMap<uint32, FReverbSourceState> SourceStates;

	float SampleRate = 48000.f;
	double LastRT60LogSeconds = 0.0;
};