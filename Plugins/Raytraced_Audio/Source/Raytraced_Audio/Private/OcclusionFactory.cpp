#include "OcclusionFactory.h"
#include "RaytracedOcclusion.h"
#include "Log.h"

TAudioOcclusionPtr FRaytracedOcclusionFactory::CreateNewOcclusionPlugin(FAudioDevice* OwningDevice)
{
	UE_LOG(LogRTA, Log, TEXT("Create New Occlusion Plugin"));
	TSharedPtr<FRaytraceManager>* Existing = RTManagerMapPtr->Find(OwningDevice);
	TSharedPtr<FRaytraceManager> Manager = Existing ? *Existing : RTManagerMapPtr->Add(OwningDevice, MakeShared<FRaytraceManager>());
		
	OwningDevice->RegisterPluginListener(AudioPluginListenerPtr);
		
	return MakeShared<FRaytracedOcclusion>(Manager);
}
