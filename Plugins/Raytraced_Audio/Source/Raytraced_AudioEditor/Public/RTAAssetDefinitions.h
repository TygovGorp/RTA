#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "RTAAssetDefinitions.generated.h"

/**
 * Since UE 5.2 the Content Browser's Add menu is built from UAssetDefinition registrations,
 * not from raw UFactory categories. A factory alone is enough for the asset to exist and be
 * creatable in code, but it will not surface in the Add menu without one of these.
 */
UCLASS()
class UAssetDefinition_RTAReverbSourceSettings : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};

UCLASS()
class UAssetDefinition_RTAOcclusionSourceSettings : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};
