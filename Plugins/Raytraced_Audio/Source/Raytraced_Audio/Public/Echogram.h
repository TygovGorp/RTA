#pragma once

#include "CoreMinimal.h"
#include "RTAAcousticBands.h"

class FEchogram
{
public:
	static constexpr float BinWidthSeconds = 0.001f;   // 1 ms per slot
	static constexpr int32 NumBins         = 3000;     // 3 s of tail
	static constexpr float MaxTimeSeconds  = BinWidthSeconds * NumBins;
	static constexpr int32 Size            = RTA::NumBands * NumBins;
	static constexpr float SmoothingAlpha  = 0.1f;
	static constexpr float MaxPathCm = MaxTimeSeconds * RTA::SpeedOfSoundCmPerSecond;

	FEchogram()
	{
		Echogram.SetNumZeroed(Size);
	}
	
	static FORCEINLINE int32 Index(int32 Band, int32 Bin)
	{
		checkSlow(Band >= 0 && Band < RTA::NumBands);
		checkSlow(Bin >= 0 && Bin < NumBins);
		return Band * NumBins + Bin;
	}

	FORCEINLINE float& At(int32 Band, int32 Bin)       { return Echogram[Index(Band, Bin)]; }
	FORCEINLINE float  At(int32 Band, int32 Bin) const { return Echogram[Index(Band, Bin)]; }

	FORCEINLINE float& operator[](int32 i)       { return Echogram[i]; }
	FORCEINLINE float  operator[](int32 i) const { return Echogram[i]; }

	auto begin() const { return Echogram.begin(); }
	auto end()   const { return Echogram.end(); }

	static int32 BinFromPathLengthCm(float PathLengthCm);

	void   Reset();
	void   Scale(float Factor);
	void   Accumulate(const FEchogram& Fresh, float Alpha);
	int32  FirstNonZeroBin(int32 Band) const;
	int32  LastNonZeroBin(int32 Band) const;
	double BandTotal(int32 Band) const;

	// Kept for existing callers.
	static constexpr float GetBinWidthSeconds() { return BinWidthSeconds; }
	static constexpr int32 GetNumBins()         { return NumBins; }
	static constexpr float GetMaxTimeSeconds()  { return MaxTimeSeconds; }
	static constexpr int32 GetSize()            { return Size; }
	static constexpr float GetSmoothingAlpha()  { return SmoothingAlpha; }

private:
	TArray<float> Echogram;
};