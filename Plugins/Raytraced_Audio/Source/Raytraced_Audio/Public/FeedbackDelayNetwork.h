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
	float Process(float In)
	{
		float Out = Buffer[WriteIndex];
		Buffer[WriteIndex] = In;
		if (++WriteIndex >= Buffer.Num()) WriteIndex = 0;
		return Out;
	}
};

class FFeedbackDelayNetwork
{
public:
	void Init(float SampleRate);
	void SetRT60(float Seconds);
	void SetDamping(float Amount);
	void SetDecayFromBands(const float RT60PerBand[6]);

	/** Gap between the dry sound and the onset of the tail. Clamped to MaxPredelaySeconds. */
	void SetPredelaySeconds(float Seconds);

	float GetMeanDelaySeconds() const;
	void Reset();
	float ProcessSample(float In);
	void ProcessBlock(const float* In, float* Out, int32 NumFrames);

private:
	static int32 NearestPrimeAtLeast(int32 Wanted);

	// 16 lines rather than 8. The number of distinct recirculation paths is what sets echo
	// density, and at 8 the early tail is sparse enough to hear as a metallic ring rather
	// than a diffuse wash. Must remain a power of two: the Hadamard transform requires it.
	static constexpr int32 NumLines = 16;

	static constexpr float MaxPredelaySeconds = 0.2f;

	static void FastHadamard(TStaticArray<float, NumLines>& V);

	TStaticArray<FRTADelayLine, NumLines> Lines;
	TStaticArray<float, NumLines> Gain, TargetGain, DelaySeconds, FilterZ1, Feed;

	// Predelay sits in front of the network, so the tail starts after the first reflection
	// would have arrived instead of on top of the dry sound.
	FRTADelayLine Predelay;
	int32 PredelaySamples = 0;

	// Damping ramps per block like the gains do. Stepping a filter coefficient each time a
	// 4 Hz probe lands is audible as a click when moving between rooms.
	float Damping = 0.f, TargetDamping = 0.f;
	float SampleRate = 48000.f;
};