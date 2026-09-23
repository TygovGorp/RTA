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
	
	float GetBinWidthSeconds() const {return BinWidthSeconds;}
	float GetNumBins() const {return NumBins;}
	float GetMaxTimeSeconds() const {return MaxTimeSeconds;}
	float GetSize() const {return Size;}
	float GetSmoothingAlpha() const {return SmoothingAlpha;}
private:
	TArray<float> Echogram;
	
	const float BinWidthSeconds = 0.001f;   // 1 ms per slot
	const int32 NumBins         = 3000;     // 3 s of tail
	const float MaxTimeSeconds  = BinWidthSeconds * NumBins;

	const int32 Size = RTA::NumBands * NumBins;

	const float SmoothingAlpha = 0.1f;

};
