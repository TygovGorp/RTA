#include "AcousticMaterialAsset.h"

#include "Log.h"
#include "RTAAcousticBands.h"
#include "UObject/ObjectSaveContext.h"

constexpr float SpeedOfSoundAir = 343.0f;
constexpr float DensityAir = 1.21f;


void UAcousticMaterialAsset::BakeData()
{
	BakeTransmissionData();
	BakeAbsorptionData();
    ValidateEnergyBudget();
    BakedScattering = Scattering;
}

void UAcousticMaterialAsset::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
    BakeData();
}

void UAcousticMaterialAsset::BakeTransmissionData()
{
    float Mass = Density * Thickness;
    float FlexuralRigidity = (YoungsModulus * (Thickness * Thickness * Thickness)) / (12 * (1 - (PoissonsRatio * PoissonsRatio)));
    float CoincidenceFrequency = (SpeedOfSoundAir * SpeedOfSoundAir / UE_TWO_PI) * FMath::Sqrt(Mass / FlexuralRigidity);
    
    UE_LOG(LogRTA, Log, TEXT("CoincidenceFrequency: %.2f"), CoincidenceFrequency);

    float LossFactor = 0.f;
    switch (MountingCondition)
    {
    case EMountingCondition::Rigid:     LossFactor = 0.01f; break;
    case EMountingCondition::Resilient: LossFactor = 0.05f; break;
    case EMountingCondition::Floating:  LossFactor = 0.15f; break;
    }

    // Sharp's single-leaf TL curve, evaluated at one frequency for one panel.
    auto EvaluateSingleLeafTL = [](float f, float PanelMass, float fc, float Eta) -> float
    {
        if (f <= fc / 2.f)
        {
            return 20.f * log10f(f * PanelMass) - 47.f;
        }
        else if (f < fc)
        {
            return 20.f * log10f(f * PanelMass) - 47.f + 40.f * log10f(2.f * f / fc);
        }
        else
        {
            return 20.f * log10f(f * PanelMass) - 47.f + 10.f * log10f(2.f * Eta * f / (UE_PI * fc));
        }
    };

    switch (ConstructionType)
    {
    case EConstructionType::SingleLeaf:
    {
        for (int32 i = 0; i < RTA::NumBands; ++i)
        {
            Transmission[i] = EvaluateSingleLeafTL(RTA::FrequencyBands[i], Mass, CoincidenceFrequency, LossFactor);
        }
        break;
    }
    case EConstructionType::DoubleLeaf:
    {
        float MassSecondLeaf = UniqueSecondLeaf ? (DensitySecondLeaf * ThicknessSecondLeaf) : Mass;

        float ResonanceFrequency = (SpeedOfSoundAir / UE_TWO_PI) *
            FMath::Sqrt((DensityAir / CavityDepth) * (1.f / Mass + 1.f / MassSecondLeaf));

        for (int32 i = 0; i < RTA::NumBands; ++i)
        {
            float f = RTA::FrequencyBands[i];
            float CombinedTL = EvaluateSingleLeafTL(f, Mass, CoincidenceFrequency, LossFactor)
                              + EvaluateSingleLeafTL(f, MassSecondLeaf, CoincidenceFrequency, LossFactor);

            if (FMath::Abs(f - ResonanceFrequency) < ResonanceFrequency * 0.5f)
                 CombinedTL -= GetMassAirMassPenalty();

            Transmission[i] = CombinedTL;
        }
        break;
    }
    }

    for (int32 i = 0; i < RTA::NumBands; ++i)
    {
        Transmission[i] = FMath::Pow(10.f, -Transmission[i] / 10.f);
    }
}

void UAcousticMaterialAsset::BakeAbsorptionData()
{
    if (bOverrideAbsorption)
    {
        for (int32 i = 0; i < RTA::NumBands; ++i) Absorption[i] = OverrideAbsorption[i];
        return;
    }

    FAcousticAbsorptionRow* Row = AbsorptionRow.GetRow<FAcousticAbsorptionRow>(TEXT("BakeAbsorptionData"));
    if (!Row)
    {
        UE_LOG(LogRTA, Warning, TEXT("AbsorptionRow not set or invalid on %s"), *GetName());
        return;
    }
    
    Absorption[0] = Row->Absorption125Hz;
    Absorption[1] = Row->Absorption250Hz;
    Absorption[2] = Row->Absorption500Hz;
    Absorption[3] = Row->Absorption1kHz;
    Absorption[4] = Row->Absorption2kHz;
    Absorption[5] = Row->Absorption4kHz;
}

void UAcousticMaterialAsset::ValidateEnergyBudget()
{
    for (int32 Band = 0; Band < RTA::NumBands; ++Band)
    {
        if (Transmission[Band] > 1.f)
            UE_LOG(LogRTA, Warning, TEXT("ValidateEnergyBudget(): Transmission[%i] = %.3f"), Band, Transmission[Band]);
        
        Absorption[Band] = FMath::Clamp(Absorption[Band], 0.f , 1.f);
        Transmission[Band] = FMath::Clamp(Transmission[Band], 0.f , 1-Absorption[Band]);
    }
}

float UAcousticMaterialAsset::GetMassAirMassPenalty() const
{
    float Penalty = -12.f; //Baseline

    if (bInsulated) Penalty += 6.f; //Insulation

    switch (Coupling)
    {
    case ELeafToLeafCoupling::RigidStuds:
        break;
    case ELeafToLeafCoupling::ResilientChannel:
        Penalty += 5.f;
        break;
    }

    return Penalty; 
}


