#pragma once

#include "Factories/Factory.h"
#include "RTAOcclusionSourceSettingsFactory.generated.h"

/**
 * Lets you right-click in the Content Browser and create a
 * UMyOcclusionSourceSettings asset. Without this factory, the class
 * is a valid UObject but has no way to be instanced as an asset, so
 * the "Occlusion Plugin Settings" picker on Attenuation assets has
 * nothing to list.
 */
UCLASS()
class RAYTRACED_AUDIOEDITOR_API UMyOcclusionSourceSettingsFactory : public UFactory
{
	GENERATED_BODY()

public:
	UMyOcclusionSourceSettingsFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;

	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
};
