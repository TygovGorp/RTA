#pragma once
#include "RaytraceManager.h"

class FAudioPluginListener : public IAudioPluginListener
{
public:
	virtual void OnDeviceShutdown(FAudioDevice* AudioDevice) override
	{
		RTManagerMapPtr->Remove(AudioDevice);
	}
	
	//This function is called when a game world initializes a listener with an audio device this
	//IAudioPluginListener is registered to. Please note that it is possible to miss this event
	//if you register this IAudioPluginListener after the listener is initialized.
	virtual void OnListenerInitialize(FAudioDevice* AudioDevice, UWorld* ListenerWorld) override
	{
		UE_LOG(LogTemp, Log, TEXT("RTA: Create New Listener"))
		TSharedPtr<FRaytraceManager>* Manager = RTManagerMapPtr->Find(AudioDevice);
		checkf(Manager != nullptr, TEXT("RTA: OnListenerInitialize, Manager not found"));
		Manager->Get()->SetWorld(ListenerWorld);
	}
	
	void SetRTManagerMapPtr(TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>>* Ptr) { RTManagerMapPtr = Ptr; }
	
private:
	TMap<FAudioDevice*, TSharedPtr<FRaytraceManager>>* RTManagerMapPtr = nullptr;
};
