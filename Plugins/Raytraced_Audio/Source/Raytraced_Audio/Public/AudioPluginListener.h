#pragma once
#include "RaytraceManager.h"

class FAudioPluginListener : public IAudioPluginListener
{
public:
	virtual void OnDeviceShutdown(FAudioDevice* AudioDevice) override;
	
	virtual void OnListenerInitialize(FAudioDevice* AudioDevice, UWorld* ListenerWorld) override;
	
	virtual void OnWorldChanged(FAudioDevice* AudioDevice, UWorld* InWorld) override;
	
	void SetRTManagerMapPtr(TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>>* Ptr) { RTManagerMapPtr = Ptr; }
	
private:
	TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>>* RTManagerMapPtr = nullptr;
};
