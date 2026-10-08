#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "RTASourceSettingsFactories.generated.h"

UCLASS(hidecategories = Object)
class URTAOcclusionSourceSettingsFactory : public UFactory
{
	GENERATED_BODY()

public:
	URTAOcclusionSourceSettingsFactory();

	virtual UObject* FactoryCreateNew(
		UClass* InClass,
		UObject* InParent,
		FName InName,
		EObjectFlags Flags,
		UObject* Context,
		FFeedbackContext* Warn) override;

	virtual uint32 GetMenuCategories() const override;
};

UCLASS(hidecategories = Object)
class URTASourceDataOverrideSettingsFactory : public UFactory
{
	GENERATED_BODY()

public:
	URTASourceDataOverrideSettingsFactory();

	virtual UObject* FactoryCreateNew(
		UClass* InClass,
		UObject* InParent,
		FName InName,
		EObjectFlags Flags,
		UObject* Context,
		FFeedbackContext* Warn) override;

	virtual uint32 GetMenuCategories() const override;
};

UCLASS(hidecategories = Object)
class URTAReverbSourceSettingsFactory : public UFactory
{
	GENERATED_BODY()

public:
	URTAReverbSourceSettingsFactory();

	virtual UObject* FactoryCreateNew(
		UClass* InClass,
		UObject* InParent,
		FName InName,
		EObjectFlags Flags,
		UObject* Context,
		FFeedbackContext* Warn) override;

	virtual uint32 GetMenuCategories() const override;
};
