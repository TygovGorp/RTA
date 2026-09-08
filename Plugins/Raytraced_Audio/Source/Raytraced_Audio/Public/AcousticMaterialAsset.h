#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "AcousticMaterialAsset.generated.h"

UENUM(BlueprintType)
enum class EConstructionType : uint8
{
	SingleLeaf UMETA(DisplayName = "SingleLeaf"),
	DoubleLeaf UMETA(DisplayName = "DoubleLeaf")
};

UENUM(BlueprintType)
enum class EMountingCondition : uint8
{
	Rigid		UMETA(DisplayName = "Rigid"),
	Resilient	UMETA(DisplayName = "Resilient"),
	Floating	UMETA(DisplayName = "Floating")
};
UENUM(BlueprintType)
enum class ELeafToLeafCoupling : uint8
{
	RigidStuds			UMETA(DisplayName = "RigidStuds"),
	ResilientChannel	UMETA(DisplayName = "ResilientChannel")
};
	

/**
 * 
 */
UCLASS()
class RAYTRACED_AUDIO_API UAcousticMaterialAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	UFUNCTION(CallInEditor, Category = "Data")
	void CookMaterialProperties();
	
#if WITH_EDITORONLY_DATA
	//General Input Fields
	UPROPERTY(EditAnywhere, Category = "Data")
	EConstructionType ConstructionType;
	UPROPERTY(EditAnywhere, Category = "Data")
	float Density; //kg/m^3
	UPROPERTY(EditAnywhere, Category = "Data")
	float Thickness; //m
	UPROPERTY(EditAnywhere, Category = "Data")
	float YoungsModulus; //Pa
	UPROPERTY(EditAnywhere, Category = "Data")
	EMountingCondition MountingCondition;
	UPROPERTY(EditAnywhere, Category = "Data")
	float PanelArea; //m^2
	
	//Double Leaf Inputs fields
	UPROPERTY(EditAnywhere, Category = "Data",
		meta = (EditCondition = "ConstructionType == EConstructionType::DoubleLeaf"))
	float CavityDepth;
	UPROPERTY(EditAnywhere, Category = "Data",
		meta = (EditCondition = "ConstructionType == EConstructionType::DoubleLeaf"))
	bool Insulated;
	UPROPERTY(EditAnywhere, Category = "Data",
		meta = (EditCondition = "ConstructionType == EConstructionType::DoubleLeaf"))
	ELeafToLeafCoupling Coupling;
#endif
	
	FVector GetTransmission() const {return Transmission;}
	FVector GetAbsorption() const {return Absorption;}
private:
	
	//Baked Output
	FVector Transmission;
	FVector Absorption;
};

UCLASS()
class UAcousticPhysicalMaterial : public UPhysicalMaterial
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Acoustics")
	TObjectPtr<UAcousticMaterialAsset> AcousticMaterial;
};