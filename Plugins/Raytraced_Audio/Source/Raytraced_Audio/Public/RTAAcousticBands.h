#pragma once

#include "CoreMinimal.h"

/**
 * Single source of truth for the plugin's frequency banding.
 */
namespace RTA
{
	inline constexpr int32 NumBands = 6;
	
	inline constexpr int FrequencyBands[NumBands] = {
		125,
		250,
		500,
		1000,
		2000,
		4000
	};
	
	/**
	 * Atmospheric absorption, energy attenuation per metre (nepers/m), ~20 C / 50% RH.
	 * Order-of-magnitude values from ISO 9613-1.
	 */
	inline constexpr float AirAbsorptionPerMetre[NumBands] = {
		0.0000691f,
		0.000253f,
		0.000645f,
		0.00115f,
		0.00207f,
		0.00527f,
	};
}
