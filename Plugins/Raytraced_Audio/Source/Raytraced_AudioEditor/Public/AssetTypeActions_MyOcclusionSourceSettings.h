#pragma once

#include "AssetTypeActions_Base.h"

class FAssetTypeActions_MyOcclusionSourceSettings : public FAssetTypeActions_Base
{
public:
	FAssetTypeActions_MyOcclusionSourceSettings(EAssetTypeCategories::Type InAssetCategory)
		: MyAssetCategory(InAssetCategory)
	{}

	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;

private:
	EAssetTypeCategories::Type MyAssetCategory;
};
