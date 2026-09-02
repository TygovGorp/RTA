#include "RaytracedReverb.h"

void FRaytracedReverb::Initialize(const FAudioPluginInitializationParams /*InitializationParams*/)
{
}

void FRaytracedReverb::Shutdown()
{
}

void FRaytracedReverb::OnDeviceShutdown(FAudioDevice* /*AudioDevice*/)
{
}

void FRaytracedReverb::OnInitSource(const uint32 /*SourceId*/, const FName& /*AudioComponentUserId*/, const uint32 /*NumChannels*/,
	UReverbPluginSourceSettingsBase* /*InSettings*/)
{
}

void FRaytracedReverb::OnReleaseSource(const uint32 /*SourceId*/)
{
}

FSoundEffectSubmixPtr FRaytracedReverb::GetEffectSubmix()
{
	return nullptr;
}

USoundSubmix* FRaytracedReverb::GetSubmix()
{
	return nullptr;
}

void FRaytracedReverb::ProcessSourceAudio(const FAudioPluginSourceInputData& InputData,
	FAudioPluginSourceOutputData& OutputData)
{
	IAudioReverb::ProcessSourceAudio(InputData, OutputData);
}
