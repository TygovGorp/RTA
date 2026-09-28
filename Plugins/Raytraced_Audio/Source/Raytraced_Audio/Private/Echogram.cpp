#include "Echogram.h"

int32 FEchogram::BinFromPathLengthCm(float PathLengthCm)
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

void FEchogram::Accumulate(const FEchogram& Fresh, float Alpha)
{
	for (int32 i = 0; i < Size; ++i)
	{
		Echogram[i] = FMath::Lerp(Echogram[i], Fresh[i], Alpha);
	}
}

int32 FEchogram::FirstNonZeroBin(int32 Band) const
{
	// Bin 0 is reserved for the direct path and never deposited into, so the first populated
	// bin is the earliest reflection the probe found.
	for (int32 Bin = 1; Bin < NumBins; ++Bin)
	{
		if (At(Band, Bin) > 0.f)
		{
			return Bin;
		}
	}
	return INDEX_NONE;
}

int32 FEchogram::LastNonZeroBin(int32 Band) const
{
	for (int32 Bin = NumBins - 1; Bin >= 0; --Bin)
	{
		if (At(Band, Bin) > 0.f)
		{
			return Bin;
		}
	}
	return INDEX_NONE;
}

double FEchogram::BandTotal(int32 Band) const
{
	double Total = 0.0;
	for (int32 Bin = 0; Bin < NumBins; ++Bin)
	{
		Total += At(Band, Bin);
	}
	return Total;
}