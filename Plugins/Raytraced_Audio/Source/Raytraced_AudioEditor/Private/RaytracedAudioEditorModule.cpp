#include "RaytracedAudioEditorModule.h"
#include "AssetTypeActions_MyOcclusionSourceSettings.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

#define LOCTEXT_NAMESPACE "FRaytracedAudioEditorModule"

void FRaytracedAudioEditorModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("RTA: Editor module started"));

	// A bare UFactory is NOT enough to appear in the Content Browser's
	// create-asset menu (this changed in UE 4.25+). You must also
	// register an FAssetTypeActions_Base for the class - that's what
	// actually gives the Content Browser an entry to hang the factory
	// off of.
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	EAssetTypeCategories::Type AudioCategory = AssetTools.RegisterAdvancedAssetCategory(
		FName(TEXT("RaytracedAudio")), LOCTEXT("RaytracedAudioAssetCategory", "Raytraced Audio"));

	TSharedPtr<IAssetTypeActions> Action = MakeShared<FAssetTypeActions_MyOcclusionSourceSettings>(AudioCategory);
	AssetTools.RegisterAssetTypeActions(Action.ToSharedRef());
	RegisteredAssetTypeActions.Add(Action);
}

void FRaytracedAudioEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (const TSharedPtr<IAssetTypeActions>& Action : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Action.ToSharedRef());
		}
	}
	RegisteredAssetTypeActions.Empty();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRaytracedAudioEditorModule, Raytraced_AudioEditor)
