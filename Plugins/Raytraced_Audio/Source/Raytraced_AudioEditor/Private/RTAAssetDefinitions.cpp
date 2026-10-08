#include "RTAAssetDefinitions.h"

#include "RTAOcclusionSourceSettings.h"
#include "RTAReverbSourceSettings.h"
#include "RTASourceDataOverrideSettings.h"

#define LOCTEXT_NAMESPACE "RTAAssetDefinitions"

FText UAssetDefinition_RTAOcclusionSourceSettings::GetAssetDisplayName() const
{
	return LOCTEXT("RTAOcclusionSourceSettings", "Raytraced Occlusion Source Settings");
}

FLinearColor UAssetDefinition_RTAOcclusionSourceSettings::GetAssetColor() const
{
	return FLinearColor(FColor(97, 85, 212));
}

TSoftClassPtr<UObject> UAssetDefinition_RTAOcclusionSourceSettings::GetAssetClass() const
{
	return URTAOcclusionSourceSettings::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_RTAOcclusionSourceSettings::GetAssetCategories() const
{
	static const auto Categories = { EAssetCategoryPaths::Audio };
	return Categories;
}

FText UAssetDefinition_RTAReverbSourceSettings::GetAssetDisplayName() const
{
	return LOCTEXT("RTAReverbSourceSettings", "Raytraced Reverb Source Settings");
}

FLinearColor UAssetDefinition_RTAReverbSourceSettings::GetAssetColor() const
{
	return FLinearColor(FColor(97, 85, 212));
}

TSoftClassPtr<UObject> UAssetDefinition_RTAReverbSourceSettings::GetAssetClass() const
{
	return URTAReverbSourceSettings::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_RTAReverbSourceSettings::GetAssetCategories() const
{
	static const auto Categories = { EAssetCategoryPaths::Audio };
	return Categories;
}

FText UAssetDefinition_RTASourceDataOverrideSettings::GetAssetDisplayName() const
{
	return LOCTEXT("RTASourceDataOverrideSettings", "Raytraced Virtual Source Settings");
}

FLinearColor UAssetDefinition_RTASourceDataOverrideSettings::GetAssetColor() const
{
	return FLinearColor(FColor(97, 85, 212));
}

TSoftClassPtr<UObject> UAssetDefinition_RTASourceDataOverrideSettings::GetAssetClass() const
{
	return URTASourceDataOverrideSettings::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_RTASourceDataOverrideSettings::GetAssetCategories() const
{
	static const auto Categories = { EAssetCategoryPaths::Audio };
	return Categories;
}

#undef LOCTEXT_NAMESPACE
