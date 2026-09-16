#pragma once
#include "RaytraceManager.h"

class FAudioPluginListener : public IAudioPluginListener
{
public:
	virtual void OnDeviceShutdown(FAudioDevice* AudioDevice) override;
	
	//This function is called when a game world initializes a listener with an audio device this
	//IAudioPluginListener is registered to. Please note that it is possible to miss this event
	//if you register this IAudioPluginListener after the listener is initialized.
	virtual void OnListenerInitialize(FAudioDevice* AudioDevice, UWorld* ListenerWorld) override;
	
	virtual void OnWorldChanged(FAudioDevice* AudioDevice, UWorld* InWorld) override;
	
	void SetRTManagerMapPtr(TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>>* Ptr) { RTManagerMapPtr = Ptr; }
	
private:
	TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>>* RTManagerMapPtr = nullptr;
};
