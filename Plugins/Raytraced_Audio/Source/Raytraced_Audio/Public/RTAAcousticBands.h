#pragma once

#include "CoreMinimal.h"

namespace RTA
{
	inline constexpr int32 NumBands = 6;

	inline constexpr float FrequencyBands[NumBands] = {
		125.f,
		250.f,
		500.f,
		1000.f,
		2000.f,
		4000.f
	};

	inline constexpr float AirAbsorptionPerMetre[NumBands] = {
		0.0000691f,
		0.000253f,
		0.000645f,
		0.00115f,
		0.00207f,
		0.00527f,
	};

	// Speed of sound in air at ~20 C.
	inline constexpr float SpeedOfSoundMetresPerSecond = 343.0f;
	inline constexpr float SpeedOfSoundCmPerSecond     = SpeedOfSoundMetresPerSecond * 100.0f;

	inline constexpr float CmToMetres = 0.01f;
	
	inline constexpr float DensityAir = 1.21f;
	
	inline constexpr float DefaultAbsorption   = 0.10f;
	inline constexpr float DefaultTransmission = 0.00f;
	inline constexpr float DefaultReflection   = 1.f - DefaultAbsorption - DefaultTransmission;
	inline constexpr float DefaultScattering   = 0.50f;
	
	inline constexpr float TeleportThresholdCm = 500.f;
	inline constexpr float ReceiverRadiusCm = 10.f;
	inline constexpr float MinReceiverDistanceCm = 50.f;

}
