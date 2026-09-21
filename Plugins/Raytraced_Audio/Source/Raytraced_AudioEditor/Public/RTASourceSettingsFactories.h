#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "RTASourceSettingsFactories.generated.h"

/**
 * Makes URTAReverbSourceSettings creatable from the Content Browser and from the
 * "Create New Asset" section of the Reverb Plugin Settings array picker on a
 * Sound Attenuation asset. Without a UFactory the class exists but the editor has
 * no way to instantiate it.
 */
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

/** Same, for the occlusion settings class. */
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
