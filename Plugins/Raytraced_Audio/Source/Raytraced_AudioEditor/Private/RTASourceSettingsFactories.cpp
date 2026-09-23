#include "RTASourceSettingsFactories.h"

#include "AssetTypeCategories.h"
#include "RTAOcclusionSourceSettings.h"

URTAOcclusionSourceSettingsFactory::URTAOcclusionSourceSettingsFactory()
{
	SupportedClass = URTAOcclusionSourceSettings::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* URTAOcclusionSourceSettingsFactory::FactoryCreateNew(
	UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	return NewObject<URTAOcclusionSourceSettings>(InParent, InName, Flags);
}

uint32 URTAOcclusionSourceSettingsFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Sounds;
}
