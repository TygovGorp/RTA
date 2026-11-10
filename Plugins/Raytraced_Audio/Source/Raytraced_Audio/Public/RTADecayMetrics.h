#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Echogram.h"

struct FDecayMetric
{
	float EDT = 0.f;
	float T20 = 0.f;
	float T30 = 0.f;

	float EDT_R = 0.f;
	float T20_R = 0.f;
	float T30_R = 0.f;

	float CurvaturePercent = 0.f;

	bool bEDTValid = false;
	bool bT20Valid = false;
	bool bT30Valid = false;

	bool bWindowSufficient = false;
};

namespace RTA
{
	using FDecayCurve = TStaticArray<float, FEchogram::NumBins>;
	
	bool ComputeEDC(const FEchogram& Echogram, int32 Band, FDecayCurve& OutEDC);
	
	void EDCToDB(FDecayCurve& EDC);
	
	bool FitDecay(const FDecayCurve& EDC_dB, float UpperDb, float LowerDb,
				  int32 LastValidBin, float& OutSlope, float& OutR);
	
	FDecayMetric ComputeDecayMetrics(const FEchogram& Echogram, int32 Band);
}