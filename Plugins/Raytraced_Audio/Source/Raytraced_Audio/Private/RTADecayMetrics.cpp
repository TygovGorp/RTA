#include "RTADecayMetrics.h"

namespace RTA
{

bool ComputeEDC(const FEchogram& Echogram, int32 Band, FDecayCurve& OutEDC)
{
	if (Band < 0 || Band >= RTA::NumBands)
	{
		return false;
	}

	double Running = 0.0;

	for (int32 Bin = FEchogram::NumBins - 1; Bin >= 0; --Bin)
	{
		Running += Echogram.At(Band, Bin);
		OutEDC[Bin] = static_cast<float>(Running);
	}

	return OutEDC[0] > 0.f;
}

void EDCToDB(FDecayCurve& EDC)
{
	const float Total = EDC[0];

	if (!(Total > 0.f))   // also catches NaN
	{
		for (int32 Bin = 0; Bin < FEchogram::NumBins; ++Bin)
		{
			EDC[Bin] = -FLT_MAX;
		}
		return;
	}

	for (int32 Bin = 0; Bin < FEchogram::NumBins; ++Bin)
	{
		EDC[Bin] = (EDC[Bin] > 0.f)
			? 10.f * FMath::LogX(10.f, EDC[Bin] / Total)
			: -FLT_MAX;
	}
}

bool FitDecay(const FDecayCurve& EDC_dB, float UpperDb, float LowerDb,
              int32 LastValidBin, float& OutSlope, float& OutR)
{
	OutSlope = 0.f;
	OutR = 0.f;

	if (LowerDb >= UpperDb)
	{
		return false;
	}
	int32 StartBin = INDEX_NONE;
	int32 EndBin = INDEX_NONE;

	for (int32 Bin = 0; Bin <= LastValidBin; ++Bin)
	{
		const float Value = EDC_dB[Bin];

		if (StartBin == INDEX_NONE && Value <= UpperDb)
		{
			StartBin = Bin;
		}
		if (Value <= LowerDb)
		{
			EndBin = Bin;
			break;
		}
	}

	if (StartBin == INDEX_NONE || EndBin == INDEX_NONE) return false;
	if (EndBin > LastValidBin)                          return false;
	if (EndBin - StartBin < 10)                         return false;
	
	double Sx = 0.0, Sy = 0.0, Sxx = 0.0, Sxy = 0.0, Syy = 0.0;
	int32 n = 0;

	for (int32 Bin = StartBin; Bin <= EndBin; ++Bin)
	{
		const double t = double(Bin - StartBin) * FEchogram::BinWidthSeconds;
		const double y = EDC_dB[Bin];

		Sx  += t;
		Sy  += y;
		Sxx += t * t;
		Sxy += t * y;
		Syy += y * y;
		++n;
	}

	const double Denominator = double(n) * Sxx - Sx * Sx;
	if (FMath::Abs(Denominator) < UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	const double Covariance = double(n) * Sxy - Sx * Sy;
	const double Slope = Covariance / Denominator;

	if (Slope >= 0.0)   // not decaying
	{
		return false;
	}

	const double YVariance = double(n) * Syy - Sy * Sy;
	OutSlope = static_cast<float>(Slope);
	OutR = (YVariance > 0.0)
		? static_cast<float>(Covariance / FMath::Sqrt(Denominator * YVariance))
		: 0.f;

	return true;
}

FDecayMetric ComputeDecayMetrics(const FEchogram& Echogram, int32 Band)
{
	FDecayMetric Result;

	FDecayCurve Curve;
	if (!ComputeEDC(Echogram, Band, Curve))
	{
		return Result;
	}
	EDCToDB(Curve);   // in place: Curve is now dB

	int32 LastValidBin = FEchogram::NumBins - 1;

	float RefSlope = 0.f, RefR = 0.f;
	if (FitDecay(Curve, 0.f, -10.f, FEchogram::NumBins - 1, RefSlope, RefR))
	{
		constexpr int32 WindowBins = 100;
		constexpr float SteepnessLimit = 1.25f;

		for (int32 Bin = WindowBins; Bin < FEchogram::NumBins; ++Bin)
		{
			const float A = Curve[Bin - WindowBins];
			const float B = Curve[Bin];
			if (A <= -FLT_MAX * 0.5f || B <= -FLT_MAX * 0.5f) { LastValidBin = Bin - WindowBins; break; }

			const float LocalSlope = (B - A) / (WindowBins * FEchogram::BinWidthSeconds);
			if (LocalSlope < RefSlope * SteepnessLimit)
			{
				LastValidBin = Bin - WindowBins;
				break;
			}
		}
	}

	Result.bWindowSufficient = (Curve[LastValidBin] <= -35.f);

	float Slope = 0.f, R = 0.f;

	if (FitDecay(Curve, 0.f, -10.f, LastValidBin, Slope, R))
	{
		Result.EDT = -60.f / Slope;
		Result.EDT_R = R;
		Result.bEDTValid = true;
	}
	if (FitDecay(Curve, -5.f, -25.f, LastValidBin, Slope, R))
	{
		Result.T20 = -60.f / Slope;
		Result.T20_R = R;
		Result.bT20Valid = true;
	}
	if (FitDecay(Curve, -5.f, -35.f, LastValidBin, Slope, R))
	{
		Result.T30 = -60.f / Slope;
		Result.T30_R = R;
		Result.bT30Valid = true;
	}

	if (Result.bT20Valid && Result.bT30Valid && Result.T20 > 0.f)
	{
		Result.CurvaturePercent = 100.f * (Result.T30 / Result.T20 - 1.f);
	}

	return Result;
}

}   // namespace RTA