#pragma once
#include "AudioDevice.h"
#include "RaytracedOcclusion.h"
#include "RaytraceManager.h"
#include "RTAOcclusionSourceSettings.h"

class FRaytracedOcclusionFactory : public IAudioOcclusionFactory
{
public:
		virtual FString GetDisplayName() override { return TEXT("MyRaytraceOcclusion"); }
	virtual bool SupportsPlatform(const FString& PlatformName) override { return true; }
	virtual TAudioOcclusionPtr CreateNewOcclusionPlugin(FAudioDevice* OwningDevice) override
	{
		UE_LOG(LogTemp, Log, TEXT("RTA: Create New Occlusion Plugin"));
		TSharedPtr<FRaytraceManager>* Existing = RTManagerMapPtr->Find(OwningDevice);
		TSharedPtr<FRaytraceManager> Manager = Existing ? *Existing : RTManagerMapPtr->Add(OwningDevice, MakeShared<FRaytraceManager>());
		
		OwningDevice->RegisterPluginListener(AudioPluginListenerPtr);
		
		return MakeShared<FRaytracedOcclusion>(Manager);
	}
	
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
