#include "RTAEchogram.h"

int32 FEchogram::Index(int32 Band, int32 Bin) const
{
	checkSlow(Band >= 0 && Band < RTA::NumBands);
	checkSlow(Bin >= 0 && Bin < NumBins);
	return Band * NumBins + Bin;
}

int32 FEchogram::BinFromPathLengthCm(float PathLengthCm) const
{
	const float TimeSeconds = PathLengthCm / RTA::SpeedOfSoundCmPerSecond;
	const int32 Bin = FMath::FloorToInt32(TimeSeconds / BinWidthSeconds);
	return (Bin >= 0 && Bin < NumBins) ? Bin : INDEX_NONE;
}

void FEchogram::Reset()
{
	check(Echogram.Num() == Size);
	FMemory::Memzero(Echogram.GetData(), Size * sizeof(float));
}

void FEchogram::Scale(float Factor)
{
	check(Echogram.Num() == Size);
	for (float& Value : Echogram)
	{
		Value *= Factor;
	}
}

void FEchogram::Accumulate(FEchogram& Fresh, float Alpha)
{
	for (int32 i = 0; i < Size; ++i)
	{
		Echogram[i] = FMath::Lerp(Echogram[i], Fresh[i], Alpha);
	}
}

int32 FEchogram::LastNonZeroBin(int32 Band)
{
	const int32 Base = Band * NumBins;
	for (int32 Bin = NumBins - 1; Bin >= 0; --Bin)
	{
		if (Echogram[Base + Bin] > 0.f)
		{
			return Bin;
		}
	}
	return INDEX_NONE;
}

double FEchogram::BandTotal(int32 Band)
{
	const int32 Base = Band * NumBins;
	double Total = 0.0;
	for (int32 Bin = 0; Bin < NumBins; ++Bin)
	{
		Total += Echogram[Base + Bin];
	}
	return Total;
}
