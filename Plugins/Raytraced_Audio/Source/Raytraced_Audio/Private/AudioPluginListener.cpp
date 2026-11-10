#include "AudioPluginListener.h"
#include "Log.h"

void FAudioPluginListener::OnDeviceShutdown(FAudioDevice* AudioDevice)
{
	RTManagerMapPtr->Remove(AudioDevice);
}

void FAudioPluginListener::OnListenerInitialize(FAudioDevice* AudioDevice, UWorld* ListenerWorld)
{
	UE_LOG(LogRTA, Log, TEXT("OnListenerInitialize: world %s"), ListenerWorld ? *ListenerWorld->GetName() : TEXT("null"));
	TSharedPtr<FRaytraceManager>* Manager = RTManagerMapPtr->Find(AudioDevice);
	checkf(Manager != nullptr, TEXT("OnListenerInitialize, Manager not found"));
	Manager->Get()->SetWorld(ListenerWorld);
}

void FAudioPluginListener::OnWorldChanged(FAudioDevice* AudioDevice, UWorld* InWorld)
{
	UE_LOG(LogRTA, Log, TEXT("OnWorldChanged: world %s"), InWorld ? *InWorld->GetName() : TEXT("null"));
	TSharedPtr<FRaytraceManager>* Manager = RTManagerMapPtr->Find(AudioDevice);
	checkf(Manager != nullptr, TEXT("OnWorldChanged, Manager not found"));
	Manager->Get()->SetWorld(InWorld);
}
