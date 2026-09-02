#pragma once

#include "Modules/ModuleManager.h"

#include "OcclusionFactory.h"
#include "ReverbFactory.h"
#include "SpatializationFactory.h"


class FRaytracedAudioModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
private:
	FRaytracedSpatializationFactory SpatializationFactory;
	FRaytracedOcclusionFactory OcclusionFactory;
	FRaytracedReverbFactory ReverbFactory;
};
