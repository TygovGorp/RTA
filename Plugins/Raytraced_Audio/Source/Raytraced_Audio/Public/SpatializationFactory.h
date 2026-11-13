#pragma once
#include "AudioDevice.h"
#include "RaytraceManager.h"
#include "RTASpatializationSourceSettings.h"

class FRaytracedSpatializationFactory : public IAudioSpatializationFactory
{
public:

	virtual UClass* GetCustomSpatializationSettingsClass() const override
	{
		return URTASpatializationSourceSettings::StaticClass();
	}
	virtual FString GetDisplayName() override { return TEXT("RaytracedSpatialization"); }
	
	virtual int32 GetMaxSupportedChannels() override
	{
		return 2;
	}
	
	virtual bool SupportsPlatform(const FString& PlatformName) override { return true; }
	
	virtual TAudioSpatializationPtr CreateNewSpatializationPlugin(FAudioDevice* OwningDevice) override;
	
	
	void SetAudioPluginListenerPtr(TAudioPluginListenerPtr Ptr) { AudioPluginListenerPtr = Ptr; }
	void SetRTManagerMapPtr(TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>>* Ptr) { RTManagerMapPtr = Ptr; }
	
private:
	TAudioPluginListenerPtr AudioPluginListenerPtr = nullptr;
	TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>>* RTManagerMapPtr = nullptr;
};
