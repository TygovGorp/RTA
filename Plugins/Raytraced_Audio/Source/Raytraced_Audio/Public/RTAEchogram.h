#pragma once

#include "CoreMinimal.h"
#include "RTAAcousticBands.h"

class FEchogram
{
public:
	FEchogram()
	{
		Echogram.SetNumZeroed(Size);
	}
	
	float& operator[](const int32 Index)
	{
		return Echogram[Index];
	}

	int32 Index(int32 Band, int32 Bin) const;
	int32 BinFromPathLengthCm(float PathLengthCm) const;
	void Reset();
	void Scale(float Factor);
	void Accumulate(FEchogram& Fresh, float Alpha);
	int32 LastNonZeroBin(int32 Band);
	double BandTotal(int32 Band);
	
	float GetSmoothingAlpha() const {return SmoothingAlpha;}
private:
	TArray<float> Echogram;
	
	constexpr float BinWidthSeconds = 0.001f;   // 1 ms per slot
	constexpr int32 NumBins         = 3000;     // 3 s of tail
	constexpr float MaxTimeSeconds  = BinWidthSeconds * NumBins;

	constexpr int32 Size = RTA::NumBands * NumBins;

	constexpr float SmoothingAlpha = 0.1f;

};
