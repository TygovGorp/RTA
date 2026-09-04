#pragma once

#include "Modules/ModuleManager.h"
#include "AssetTypeCategories.h"

class FRaytracedAudioEditorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Handles to the asset type actions we register, so we can unregister them cleanly on shutdown. */
	TArray<TSharedPtr<class IAssetTypeActions>> RegisteredAssetTypeActions;
};
