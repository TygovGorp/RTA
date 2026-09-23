#include "AcousticMaterialAsset.h"

#include "Log.h"
#include "RTAAcousticBands.h"
#include "UObject/ObjectSaveContext.h"

void UAcousticMaterialAsset::BakeData()
{
#if WITH_EDITOR
	BakeTransmissionData();
	BakeAbsorptionData();
	ValidateEnergyBudget();
	BakedScattering = FMath::Clamp(Scattering, 0.f, 1.f);
#endif
}

void UAcousticMaterialAsset::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
	BakeData();
}

#if WITH_EDITOR

void UAcousticMaterialAsset::BakeTransmissionData()
{
	const float Mass = Density * Thickness;
	const float PoissonTerm = 1.f - (PoissonsRatio * PoissonsRatio);
	const float FlexuralRigidity = (PoissonTerm > KINDA_SMALL_NUMBER)
		? (YoungsModulus * (Thickness * Thickness * Thickness)) / (12.f * PoissonTerm)
		: 0.f;

	if (Mass <= KINDA_SMALL_NUMBER || FlexuralRigidity <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogRTA, Warning,
			TEXT("%s: transmission inputs are degenerate (Mass=%.4f, FlexuralRigidity=%.4f, PoissonsRatio=%.3f). "
			     "Transmission left at zero. Set Density, Thickness and YoungsModulus > 0 and PoissonsRatio < 1."),
			*GetName(), Mass, FlexuralRigidity, PoissonsRatio);

		for (int32 i = 0; i < RTA::NumBands; ++i)
		{
			Transmission[i] = 0.f;
		}
		return;
	}

	const float CoincidenceFrequency =
		(RTA::SpeedOfSoundMetresPerSecond * RTA::SpeedOfSoundMetresPerSecond / UE_TWO_PI)
		* FMath::Sqrt(Mass / FlexuralRigidity);

	UE_LOG(LogRTA, Log, TEXT("%s: CoincidenceFrequency = %.2f Hz"), *GetName(), CoincidenceFrequency);

	float LossFactor = 0.f;
	switch (MountingCondition)
	{
	case EMountingCondition::Rigid:     LossFactor = 0.01f; break;
	case EMountingCondition::Resilient: LossFactor = 0.05f; break;
	case EMountingCondition::Floating:  LossFactor = 0.15f; break;
	}

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
		const float MassSecondLeaf = UniqueSecondLeaf ? (DensitySecondLeaf * ThicknessSecondLeaf) : Mass;

		if (MassSecondLeaf <= KINDA_SMALL_NUMBER || CavityDepth <= KINDA_SMALL_NUMBER)
		{
			UE_LOG(LogRTA, Warning,
				TEXT("%s: double-leaf inputs are degenerate (SecondLeafMass=%.4f, CavityDepth=%.4f). "
				     "Transmission left at zero."), *GetName(), MassSecondLeaf, CavityDepth);
			for (int32 i = 0; i < RTA::NumBands; ++i)
			{
				Transmission[i] = 0.f;
			}
			return;
		}

		const float ResonanceFrequency = (RTA::SpeedOfSoundMetresPerSecond / UE_TWO_PI) *
			FMath::Sqrt((RTA::DensityAir / CavityDepth) * (1.f / Mass + 1.f / MassSecondLeaf));

		for (int32 i = 0; i < RTA::NumBands; ++i)
		{
			const float f = RTA::FrequencyBands[i];
			float CombinedTL = EvaluateSingleLeafTL(f, Mass, CoincidenceFrequency, LossFactor)
			                 + EvaluateSingleLeafTL(f, MassSecondLeaf, CoincidenceFrequency, LossFactor);

			if (FMath::Abs(f - ResonanceFrequency) < ResonanceFrequency * 0.5f)
			{
				CombinedTL -= GetMassAirMassPenalty();
			}

			Transmission[i] = CombinedTL;
		}
		break;
	}
	}

	// dB -> energy transmission coefficient.
	for (int32 i = 0; i < RTA::NumBands; ++i)
	{
		Transmission[i] = FMath::Pow(10.f, -Transmission[i] / 10.f);
	}
}

void UAcousticMaterialAsset::BakeAbsorptionData()
{
	if (bOverrideAbsorption)
	{
		for (int32 i = 0; i < RTA::NumBands; ++i)
		{
			Absorption[i] = OverrideAbsorption[i];
		}
		return;
	}

	FAcousticAbsorptionRow* Row = AbsorptionRow.GetRow<FAcousticAbsorptionRow>(TEXT("BakeAbsorptionData"));
	if (!Row)
	{
		UE_LOG(LogRTA, Warning, TEXT("AbsorptionRow not set or invalid on %s"), *GetName());
		return;
	}

	static_assert(RTA::NumBands == 6, "Absorption table has six octave bands.");
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
		const float RawTransmission = Transmission[Band];

		if (!(RawTransmission >= 0.f) || RawTransmission > 1.f)
		{
			UE_LOG(LogRTA, Warning,
				TEXT("%s band %d (%.0f Hz): transmission model produced %.3f, outside [0,1]. "
				     "Panel is likely too light or too thin for Sharp's curve at this frequency."),
				*GetName(), Band, RTA::FrequencyBands[Band], RawTransmission);
		}

		Absorption[Band] = FMath::Clamp(Absorption[Band], 0.f, 1.f);
		Transmission[Band] = FMath::Clamp(Transmission[Band], 0.f, 1.f - Absorption[Band]);
	}
}

float UAcousticMaterialAsset::GetMassAirMassPenalty() const
{
	float Penalty = -12.f; // Baseline

	if (bInsulated) Penalty += 6.f; // Insulation

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

#endif // WITH_EDITOR
