#include "RaytracedOcclusion.h"

void FRaytracedOcclusion::Initialize(const FAudioPluginInitializationParams /*InitializationParams*/)
{
	
}

void FRaytracedOcclusion::Shutdown()
{
}

void FRaytracedOcclusion::OnInitSource(const uint32 /*SourceId*/, const FName& /*AudioComponentUserId*/,
	const uint32 /*NumChannels*/, UOcclusionPluginSourceSettingsBase* /*InSettings*/)
{
}

void FRaytracedOcclusion::OnReleaseSource(const uint32 /*SourceId*/)
{
}

void FRaytracedOcclusion::ProcessAudio(const FAudioPluginSourceInputData& /*InputData*/,
	FAudioPluginSourceOutputData& /*OutputData*/)
{
}
