#pragma once
#include "RaytracedReverb.h"

class FRaytracedReverbFactory : public IAudioReverbFactory
{
public:
	virtual FString GetDisplayName() override { return TEXT("MyRaytraceReverb"); }
	virtual bool SupportsPlatform(const FString& PlatformName) override { return true; }
	virtual TAudioReverbPtr CreateNewReverbPlugin(FAudioDevice* OwningDevice) override
	{
		return MakeShared<FRaytracedReverb>();
	}
};
