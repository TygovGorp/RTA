#include "RaytracedSpatialization.h"

void FRaytracedSpatialization::Shutdown()
{
}

void FRaytracedSpatialization::OnDeviceShutdown(FAudioDevice* /*AudioDevice*/)
{
}

void FRaytracedSpatialization::SetSpatializationParameters(uint32 /*SourceId*/, const FSpatializationParams& /*Params*/)
{
}

void FRaytracedSpatialization::GetSpatializationParameters(uint32 /*SourceId*/, FSpatializationParams& /*OutParams*/)
{
}

void FRaytracedSpatialization::InitializeSpatializationEffect(uint32 /*BufferLength*/)
{
}

void FRaytracedSpatialization::ProcessSpatializationForVoice(uint32 /*SourceId*/, float* /*InSamples*/, float* /*OutSamples*/,
	const FVector& /*Position*/)
{
}

void FRaytracedSpatialization::ProcessSpatializationForVoice(uint32 /*SourceId*/, float* /*InSamples*/, float* /*OutSamples*/)
{
}

void FRaytracedSpatialization::OnInitSource(const uint32 /*SourceId*/, const FName& /*AudioComponentUserId*/,
	USpatializationPluginSourceSettingsBase* /*InSettings*/)
{
}

void FRaytracedSpatialization::OnReleaseSource(const uint32 /*SourceId*/)
{
}

void FRaytracedSpatialization::ProcessAudio(const FAudioPluginSourceInputData& /*InputData*/,
	FAudioPluginSourceOutputData& /*OutputData*/)
{
}

void FRaytracedSpatialization::OnAllSourcesProcessed()
{
}

bool FRaytracedSpatialization::IsSpatializationEffectInitialized() const
{
	return false;
}

void FRaytracedSpatialization::Initialize(const FAudioPluginInitializationParams /*InitializationParams*/)
{
}

bool FRaytracedSpatialization::CreateSpatializationEffect(uint32 SourceId)
{
	return true;
}

void* FRaytracedSpatialization::GetSpatializationEffect(uint32 SourceId)
{
	return nullptr;
}
