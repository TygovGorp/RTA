#pragma once
#include "AudioDevice.h"
#include "RaytraceManager.h"
#include "RTASourceDataOverrideSettings.h"

class FRaytracedSourceDataOverrideFactory : public IAudioSourceDataOverrideFactory
{
public:
	virtual FString GetDisplayName() override { return TEXT("MyRaytraceVirtualSource"); }
	virtual bool SupportsPlatform(const FString& PlatformName) override { return true; }
	virtual TAudioSourceDataOverridePtr CreateNewSourceDataOverridePlugin(FAudioDevice* OwningDevice) override;

	virtual UClass* GetCustomSourceDataOverrideSettingsClass() const override
	{
		return URTASourceDataOverrideSettings::StaticClass();
	}

	void SetRTManagerMapPtr(TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>>* Ptr) { RTManagerMapPtr = Ptr; }

private:
	TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>>* RTManagerMapPtr = nullptr;
};
