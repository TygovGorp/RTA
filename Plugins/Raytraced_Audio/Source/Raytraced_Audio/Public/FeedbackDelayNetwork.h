#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"

struct FRTADelayLine
{
	TArray<float> Buffer;
	int32 WriteIndex = 0;

	void Init(int32 LengthSamples)
	{
		Buffer.SetNumZeroed(FMath::Max(LengthSamples, 1)); WriteIndex = 0;
	}
	void Reset()
	{
		FMemory::Memzero(Buffer.GetData(), Buffer.Num() * sizeof(float)); WriteIndex = 0;
	}
	void Write(float In)
	{
		Buffer[WriteIndex] = In;
		if (++WriteIndex >= Buffer.Num()) WriteIndex = 0;
	}

	float ReadFractional(float DelaySamples) const
	{
		const int32 Len = Buffer.Num();
		float Pos = float(WriteIndex) - DelaySamples;
		while (Pos < 0.f) Pos += float(Len);

		const int32 i0 = int32(Pos) % Len;
		const int32 i1 = (i0 + 1) % Len;
		const float Frac = Pos - FMath::FloorToFloat(Pos);

		return FMath::Lerp(Buffer[i0], Buffer[i1], Frac);
	}
};

class FFeedbackDelayNetwork
{
public:
	void Init(float SampleRate);
	void SetRT60(float Seconds);
	void SetDamping(float Amount);

	/** Current damping target, as derived by SetDecayFromBands. */
	float GetDamping() const { return TargetDamping; }
	void SetDecayFromBands(const float RT60PerBand[6]);

	/** Gap between the dry sound and the onset of the tail. Clamped to MaxPredelaySeconds. */
	void SetPredelaySeconds(float Seconds);

	float GetMeanDelaySeconds() const;
	void Reset();
	float ProcessSample(float In);
	void ProcessBlock(const float* In, float* Out, int32 NumFrames);

private:
	static int32 NearestPrimeAtLeast(int32 Wanted);
	
	static constexpr int32 NumLines = 16;

	static constexpr float MaxPredelaySeconds = 0.2f;

	static void FastHadamard(TStaticArray<float, NumLines>& V);

	TStaticArray<FRTADelayLine, NumLines> Lines;
	TStaticArray<float, NumLines> Gain, TargetGain, DelaySeconds, FilterZ1, Feed, BaseDelaySamples, ModRate, ModPhase;
	float ModTime = 0.f, MaxModDepthFraction = 0.02f, ModDepthFraction = 0.012f;
	
	FRTADelayLine Predelay;
	int32 PredelaySamples = 0;
	
	float Damping = 0.f, TargetDamping = 0.f;
	float SampleRate = 48000.f;
};