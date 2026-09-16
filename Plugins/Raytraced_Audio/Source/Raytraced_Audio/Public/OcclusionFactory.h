#pragma once
#include "AudioDevice.h"
#include "RaytraceManager.h"
#include "RTAOcclusionSourceSettings.h"

class FRaytracedOcclusionFactory : public IAudioOcclusionFactory
{
public:
		virtual FString GetDisplayName() override { return TEXT("MyRaytraceOcclusion"); }
	virtual bool SupportsPlatform(const FString& PlatformName) override { return true; }
	virtual TAudioOcclusionPtr CreateNewOcclusionPlugin(FAudioDevice* OwningDevice) override;
	
	virtual UClass* GetCustomOcclusionSettingsClass() const override
	{
		return URTAOcclusionSourceSettings::StaticClass();
	}
	
	void SetAudioPluginListenerPtr(TAudioPluginListenerPtr Ptr) { AudioPluginListenerPtr = Ptr; }
	void SetRTManagerMapPtr(TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>>* Ptr) { RTManagerMapPtr = Ptr; }
	
private:
	TAudioPluginListenerPtr AudioPluginListenerPtr = nullptr;
	TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>>* RTManagerMapPtr = nullptr;
};
