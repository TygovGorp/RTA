#pragma once
#include "RaytracedSpatialization.h"

class FRaytracedSpatializationFactory : public IAudioSpatializationFactory
{
public:
	virtual FString GetDisplayName() override { return TEXT("MyRaytraceSpatialization"); }
	virtual bool SupportsPlatform(const FString& PlatformName) override { return true; }
	virtual TAudioSpatializationPtr CreateNewSpatializationPlugin(FAudioDevice* OwningDevice) override
	{
		return MakeShared<FRaytracedSpatialization>();
	}
	
};
