#include "RTAOcclusionSourceSettingsFactory.h"
#include "RTAOcclusionSourceSettings.h"
#include "AssetTypeCategories.h"

UMyOcclusionSourceSettingsFactory::UMyOcclusionSourceSettingsFactory()
{
	SupportedClass = URTAOcclusionSourceSettings::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UMyOcclusionSourceSettingsFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name,
	EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<URTAOcclusionSourceSettings>(InParent, Class, Name, Flags);
}

FText UMyOcclusionSourceSettingsFactory::GetDisplayName() const
{
	return NSLOCTEXT("RaytracedAudioEditor", "MyOcclusionSourceSettingsFactory", "Raytraced Occlusion Source Settings");
}

uint32 UMyOcclusionSourceSettingsFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Sounds;
}
