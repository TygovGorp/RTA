#include "AcousticMaterialAsset.h"

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
    
    UE_LOG(LogTemp, Log, TEXT("RTA: CoincidenceFrequency: %.2f"), CoincidenceFrequency);

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
        for (int32 i = 0; i < FrequencyBands.Num(); ++i)
        {
            Transmission[i] = EvaluateSingleLeafTL(FrequencyBands[i], Mass, CoincidenceFrequency, LossFactor);
        }
        break;
    }
    case EConstructionType::DoubleLeaf:
    {
        float MassSecondLeaf = UniqueSecondLeaf ? (DensitySecondLeaf * ThicknessSecondLeaf) : Mass;

        float ResonanceFrequency = (SpeedOfSoundAir / UE_TWO_PI) *
            FMath::Sqrt((DensityAir / CavityDepth) * (1.f / Mass + 1.f / MassSecondLeaf));

        for (int32 i = 0; i < FrequencyBands.Num(); ++i)
        {
            float f = FrequencyBands[i];
            float CombinedTL = EvaluateSingleLeafTL(f, Mass, CoincidenceFrequency, LossFactor)
                              + EvaluateSingleLeafTL(f, MassSecondLeaf, CoincidenceFrequency, LossFactor);

            if (FMath::Abs(f - ResonanceFrequency) < ResonanceFrequency * 0.5f)
                 CombinedTL -= GetMassAirMassPenalty();

            Transmission[i] = CombinedTL;
        }
        break;
    }
    }

    for (int32 i = 0; i < 3; ++i)
    {
        Transmission[i] = FMath::Pow(10.f, -Transmission[i] / 10.f);
    }
}

namespace
{
    float InterpolateLogFrequency(float TargetFreq, const float* Freqs, const float* Values, int32 Count)
    {
        if (TargetFreq <= Freqs[0])          return Values[0];
        if (TargetFreq >= Freqs[Count - 1])  return Values[Count - 1];

        for (int32 i = 0; i < Count - 1; ++i)
        {
            if (TargetFreq >= Freqs[i] && TargetFreq <= Freqs[i + 1])
            {
                float LogF0 = FMath::Loge(Freqs[i]);
                float LogF1 = FMath::Loge(Freqs[i + 1]);
                float LogT  = FMath::Loge(TargetFreq);
                float Alpha = (LogT - LogF0) / (LogF1 - LogF0);
                return FMath::Lerp(Values[i], Values[i + 1], Alpha);
            }
        }
        return Values[Count - 1];
    }
}

void UAcousticMaterialAsset::BakeAbsorptionData()
{
    if (bOverrideAbsorption)
    {
        for (int32 i = 0; i < 3; ++i) Absorption[i] = OverrideAbsorption[i];
        return;
    }

    FAcousticAbsorptionRow* Row = AbsorptionRow.GetRow<FAcousticAbsorptionRow>(TEXT("BakeAbsorptionData"));
    if (!Row)
    {
        UE_LOG(LogTemp, Warning, TEXT("RTA: AbsorptionRow not set or invalid on %s"), *GetName());
        return;
    }

    const float PublishedFrequencies[6] = { 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f };
    const float PublishedValues[6] =
    {
        Row->Absorption125Hz, Row->Absorption250Hz, Row->Absorption500Hz,
        Row->Absorption1kHz,  Row->Absorption2kHz,  Row->Absorption4kHz
    };

    for (int32 i = 0; i < FrequencyBands.Num(); ++i)
    {
        Absorption[i] = InterpolateLogFrequency((float)FrequencyBands[i], PublishedFrequencies, PublishedValues, 6);
    }
}

void UAcousticMaterialAsset::ValidateEnergyBudget()
{
    for (int32 Band = 0; Band < 3; ++Band)
    {
        const float Sum = Absorption[Band] + Transmission[Band];

        if (Sum > 1.f)
        {
            const float ScaleFactor = 1.f / Sum;
            const float OldAbsorption = Absorption[Band];
            const float OldTransmission = Transmission[Band];

            Absorption[Band] *= ScaleFactor;
            Transmission[Band] *= ScaleFactor;

            UE_LOG(LogTemp, Warning,
                TEXT("RTA: %s band %d: Absorption(%.3f) + Transmission(%.3f) = %.3f exceeds 1.0. Rescaled to Absorption=%.3f, Transmission=%.3f."),
                *GetName(), Band, OldAbsorption, OldTransmission, Sum, Absorption[Band], Transmission[Band]);
        }
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


