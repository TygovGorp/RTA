#include "AudioPluginListener.h"
#include "Log.h"

void FAudioPluginListener::OnDeviceShutdown(FAudioDevice* AudioDevice)
{
	RTManagerMapPtr->Remove(AudioDevice);
}

void FAudioPluginListener::OnListenerInitialize(FAudioDevice* AudioDevice, UWorld* ListenerWorld)
{
	UE_LOG(LogRTA, Log, TEXT("Create New Listener"))
	TSharedPtr<FRaytraceManager>* Manager = RTManagerMapPtr->Find(AudioDevice);
	checkf(Manager != nullptr, TEXT("OnListenerInitialize, Manager not found"));
	Manager->Get()->SetWorld(ListenerWorld);
}

void FAudioPluginListener::OnWorldChanged(FAudioDevice* AudioDevice, UWorld* InWorld)
{
	UE_LOG(LogRTA, Log, TEXT("Create New Listener"))
	TSharedPtr<FRaytraceManager>* Manager = RTManagerMapPtr->Find(AudioDevice);
	checkf(Manager != nullptr, TEXT("OnListenerInitialize, Manager not found"));
	Manager->Get()->SetWorld(InWorld);
}
