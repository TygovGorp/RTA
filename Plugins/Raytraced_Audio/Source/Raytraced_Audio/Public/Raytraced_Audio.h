#pragma once

#include "AudioPluginListener.h"
#include "Modules/ModuleManager.h"

#include "OcclusionFactory.h"
#include "RaytraceManager.h"
#include "ReverbFactory.h"

class FRaytracedAudioModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>> RTManagerMap;

	TSharedPtr<FAudioPluginListener> AudioPluginListener;
	FRaytracedOcclusionFactory OcclusionFactory;
	FRaytracedReverbFactory ReverbFactory;

	TUniquePtr<FAutoConsoleCommand> DumpEchogramCommand;
	TUniquePtr<FAutoConsoleCommand> TestFDNCommand;
};
