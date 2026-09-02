#include "Raytraced_Audio.h"

#define LOCTEXT_NAMESPACE "FRaytraced_AudioModule"

void FRaytracedAudioModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	IModularFeatures::Get().RegisterModularFeature(
		IAudioSpatializationFactory::GetModularFeatureName(), &SpatializationFactory);
	IModularFeatures::Get().RegisterModularFeature(
		IAudioOcclusionFactory::GetModularFeatureName(), &OcclusionFactory);
	IModularFeatures::Get().RegisterModularFeature(
		IAudioReverbFactory::GetModularFeatureName(), &ReverbFactory);
}

void FRaytracedAudioModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FRaytracedAudioModule, Raytraced_Audio)