#include "SourceDataOverrideFactory.h"
#include "RaytracedSourceDataOverride.h"
#include "Log.h"

TAudioSourceDataOverridePtr FRaytracedSourceDataOverrideFactory::CreateNewSourceDataOverridePlugin(FAudioDevice* OwningDevice)
{
	UE_LOG(LogRTA, Log, TEXT("Create New Virtual Source Plugin"));

	// Same manager as the occlusion and reverb plugins on this device, whichever is created first.
	// No plugin listener here: the occlusion factory already registers it, and twice would
	// duplicate the world callbacks.
	TSharedPtr<FRaytraceManager>* Existing = RTManagerMapPtr->Find(OwningDevice);
	TSharedPtr<FRaytraceManager> Manager = Existing ? *Existing : RTManagerMapPtr->Add(OwningDevice, MakeShared<FRaytraceManager>());

	return MakeShared<FRaytracedSourceDataOverride>(Manager);
}
