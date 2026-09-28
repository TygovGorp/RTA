#pragma once
#include "AudioDevice.h"
#include "RaytracedReverb.h"
#include "RaytraceManager.h"
#include "RTAReverbSourceSettings.h"

class FRaytracedReverbFactory : public IAudioReverbFactory
{
public:
	FString GetDisplayName() override { return TEXT("MyRaytraceReverb"); }
	virtual bool SupportsPlatform(const FString& PlatformName) override { return true; }
	virtual TAudioReverbPtr CreateNewReverbPlugin(FAudioDevice* OwningDevice) override
	{
		TSharedPtr<FRaytraceManager>* Existing = RTManagerMapPtr->Find(OwningDevice);
		TSharedPtr<FRaytraceManager> Manager = Existing ? *Existing : RTManagerMapPtr->Add(OwningDevice, MakeShared<FRaytraceManager>());
		
		OwningDevice->RegisterPluginListener(AudioPluginListenerPtr);
		
		return MakeShared<FRaytracedReverb>(Manager);
	}
	virtual UClass* GetCustomReverbSettingsClass() const override { return URTAReverbSourceSettings::StaticClass(); }
	void SetAudioPluginListenerPtr(TAudioPluginListenerPtr Ptr) { AudioPluginListenerPtr = Ptr; }
	void SetRTManagerMapPtr(TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>>* Ptr) { RTManagerMapPtr = Ptr; }
	
private:
	TAudioPluginListenerPtr AudioPluginListenerPtr = nullptr;
	TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>>* RTManagerMapPtr = nullptr;
};
