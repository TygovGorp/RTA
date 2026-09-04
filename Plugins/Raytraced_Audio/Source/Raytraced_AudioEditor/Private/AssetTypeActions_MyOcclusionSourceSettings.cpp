#include "AssetTypeActions_MyOcclusionSourceSettings.h"
#include "RTAOcclusionSourceSettings.h"

FText FAssetTypeActions_MyOcclusionSourceSettings::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_MyOcclusionSourceSettings", "Raytraced Occlusion Source Settings");
}

FColor FAssetTypeActions_MyOcclusionSourceSettings::GetTypeColor() const
{
	return FColor(200, 80, 80);
}

UClass* FAssetTypeActions_MyOcclusionSourceSettings::GetSupportedClass() const
{
	return URTAOcclusionSourceSettings::StaticClass();
}

uint32 FAssetTypeActions_MyOcclusionSourceSettings::GetCategories()
{
	return MyAssetCategory;
}
