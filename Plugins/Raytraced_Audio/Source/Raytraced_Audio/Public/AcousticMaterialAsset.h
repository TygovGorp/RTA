#pragma once

#include "CoreMinimal.h"
#include "RTAAcousticBands.h"
#include "Engine/DataAsset.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/DataTable.h"
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
	
USTRUCT(BlueprintType)
struct FAcousticAbsorptionRow : public FTableRowBase
{
	GENERATED_BODY()

	// NRC-standard 1/3-octave bands.
	UPROPERTY(EditAnywhere) float Absorption125Hz = 0.f;
	UPROPERTY(EditAnywhere) float Absorption250Hz = 0.f;
	UPROPERTY(EditAnywhere) float Absorption500Hz = 0.f;
	UPROPERTY(EditAnywhere) float Absorption1kHz  = 0.f;
	UPROPERTY(EditAnywhere) float Absorption2kHz  = 0.f;
	UPROPERTY(EditAnywhere) float Absorption4kHz  = 0.f;
};


/**
 * 
 */
UCLASS()
class RAYTRACED_AUDIO_API UAcousticMaterialAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	
	UFUNCTION(CallInEditor, Category = "General")
	void BakeData();
	
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	
#if WITH_EDITORONLY_DATA
	//General Input Fields
	UPROPERTY(EditAnywhere, Category = "Transmission")
	EConstructionType ConstructionType;
	UPROPERTY(EditAnywhere, Category = "Transmission", meta = (ClampMin = "0.0"))
	float Density; //kg/m^3
	UPROPERTY(EditAnywhere, Category = "Transmission", meta = (ClampMin = "0.0"))
	float Thickness; //m
	UPROPERTY(EditAnywhere, Category = "Transmission")
	float YoungsModulus; //Pa
	UPROPERTY(EditAnywhere, Category = "Transmission")
	float PoissonsRatio;
	UPROPERTY(EditAnywhere, Category = "Transmission")
	EMountingCondition MountingCondition;
	UPROPERTY(EditAnywhere, Category = "Transmission", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Scattering = 0.5f;
	
	//Double Leaf Inputs fields
	UPROPERTY(EditAnywhere, Category = "Transmission",
		meta = (EditCondition = "ConstructionType == EConstructionType::DoubleLeaf"))
	float CavityDepth;
	UPROPERTY(EditAnywhere, Category = "Transmission",
		meta = (EditCondition = "ConstructionType == EConstructionType::DoubleLeaf"))
	bool UniqueSecondLeaf;
	UPROPERTY(EditAnywhere, Category = "Transmission",
		meta = (EditCondition = "ConstructionType == EConstructionType::DoubleLeaf && UniqueSecondLeaf == true"))
	float DensitySecondLeaf; //kg/m^3
	UPROPERTY(EditAnywhere, Category = "Transmission",
		meta = (EditCondition = "ConstructionType == EConstructionType::DoubleLeaf && UniqueSecondLeaf == true", ClampMin = "0.0"))
	float ThicknessSecondLeaf; //m
	UPROPERTY(EditAnywhere, Category = "Transmission",
		meta = (EditCondition = "ConstructionType == EConstructionType::DoubleLeaf && UniqueSecondLeaf == true"))
	float YoungsModulusSecondLeaf; //Pa
	UPROPERTY(EditAnywhere, Category = "Transmission",
		meta = (EditCondition = "ConstructionType == EConstructionType::DoubleLeaf && UniqueSecondLeaf == true"))
	float PoissonsRatioSecondLeaf; //Pa
	UPROPERTY(EditAnywhere, Category = "Transmission",
		meta = (EditCondition = "ConstructionType == EConstructionType::DoubleLeaf"))
	bool bInsulated;
	UPROPERTY(EditAnywhere, Category = "Transmission",
		meta = (EditCondition = "ConstructionType == EConstructionType::DoubleLeaf"))
	ELeafToLeafCoupling Coupling;
	
	UPROPERTY(EditAnywhere, Category = "Absorption", meta = (RowType = "AcousticAbsorptionRow"))
	FDataTableRowHandle AbsorptionRow;

	UPROPERTY(EditAnywhere, Category = "Absorption")
	bool bOverrideAbsorption = false;

	UPROPERTY(EditAnywhere, Category = "Absorption", meta = (EditCondition = "bOverrideAbsorption", ClampMin = "0.0", ClampMax = "1.0"))
	float OverrideAbsorption[RTA::NumBands] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
#endif
	
	TArrayView<const float> GetTransmission() const { return TArrayView(Transmission, RTA::NumBands); }
	TArrayView<const float> GetAbsorption()  const { return TArrayView(Absorption, RTA::NumBands); }
	float GetBakedScattering()  const { return BakedScattering; }

private:
#if WITH_EDITOR
	
	void BakeTransmissionData();
	void BakeAbsorptionData();
#if WITH_EDITOR
	void ValidateEnergyBudget();
#endif
	
	float GetMassAirMassPenalty() const;
#endif
	
	//Baked Output
	UPROPERTY(VisibleAnywhere, Category = "Baked")
	float Transmission[RTA::NumBands] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
	UPROPERTY(VisibleAnywhere, Category = "Baked")
	float Absorption[RTA::NumBands] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
	UPROPERTY(VisibleAnywhere, Category = "Baked")
	float BakedScattering = 0.5f;
};

UCLASS()
class UAcousticPhysicalMaterial : public UPhysicalMaterial
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Acoustics")
	TObjectPtr<UAcousticMaterialAsset> AcousticMaterial;
};