#include "RTASourceSettingsFactories.h"

#include "AssetTypeCategories.h"
#include "RTAOcclusionSourceSettings.h"
#include "RTAReverbSourceSettings.h"
#include "RTASourceDataOverrideSettings.h"

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

URTAReverbSourceSettingsFactory::URTAReverbSourceSettingsFactory()
{
	SupportedClass = URTAReverbSourceSettings::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* URTAReverbSourceSettingsFactory::FactoryCreateNew(
	UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	return NewObject<URTAReverbSourceSettings>(InParent, InName, Flags);
}

uint32 URTAReverbSourceSettingsFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Sounds;
}

URTASourceDataOverrideSettingsFactory::URTASourceDataOverrideSettingsFactory()
{
	SupportedClass = URTASourceDataOverrideSettings::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* URTASourceDataOverrideSettingsFactory::FactoryCreateNew(
	UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	return NewObject<URTASourceDataOverrideSettings>(InParent, InName, Flags);
}

uint32 URTASourceDataOverrideSettingsFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Sounds;
}
