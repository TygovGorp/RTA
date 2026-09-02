#pragma once
#include "RaytracedOcclusion.h"

class FRaytracedOcclusionFactory : public IAudioOcclusionFactory
{
public:
	virtual FString GetDisplayName() override { return TEXT("MyRaytraceOcclusion"); }
	virtual bool SupportsPlatform(const FString& PlatformName) override { return true; }
	virtual TAudioOcclusionPtr CreateNewOcclusionPlugin(FAudioDevice* OwningDevice) override
	{
		return MakeShared<FRaytracedOcclusion>();
	}
};
