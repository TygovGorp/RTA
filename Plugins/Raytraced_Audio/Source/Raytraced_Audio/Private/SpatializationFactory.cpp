#include "SpatializationFactory.h"
#include "RaytracedSpatialization.h"
#include "Log.h"

TAudioSpatializationPtr FRaytracedSpatializationFactory::CreateNewSpatializationPlugin(FAudioDevice* OwningDevice)
{
	UE_LOG(LogRTA, Log, TEXT("Create New Spatialization Plugin"));
	TSharedPtr<FRaytraceManager>* Existing = RTManagerMapPtr->Find(OwningDevice);
	TSharedPtr<FRaytraceManager> Manager = Existing ? *Existing : RTManagerMapPtr->Add(OwningDevice, MakeShared<FRaytraceManager>());
		
	OwningDevice->RegisterPluginListener(AudioPluginListenerPtr);
		
	return MakeShared<FRaytracedSpatialization>(Manager);
}
