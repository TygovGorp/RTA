#include "FeedbackDelayNetwork.h"

void FFeedbackDelayNetwork::Init(float InSampleRate)
{
	SampleRate = InSampleRate > 0.f ? InSampleRate : 48000.f;
	
	static constexpr int32 BasePrimes[NumLines] = {
		 911, 1087, 1201, 1373, 1459, 1607, 1777, 1949,
		2131, 2309, 2503, 2687, 2909, 3121, 3323, 3761
	};

	for (int32 i = 0; i < NumLines; ++i)
	{
		const int32 Wanted = FMath::RoundToInt(BasePrimes[i] * SampleRate / 48000.f);
		const int32 Len = NearestPrimeAtLeast(Wanted);

		Lines[i].Init(Len);
		DelaySeconds[i] = float(Len) / SampleRate;
	}

	Predelay.Init(FMath::CeilToInt(MaxPredelaySeconds * SampleRate));
	PredelaySamples = 0;

	SetRT60(2.0f);
	SetDamping(0.f);
	Reset();
}

void FFeedbackDelayNetwork::SetRT60(float Seconds)
{
	const float RT60 = FMath::Clamp(Seconds, 0.05f, 8.0f);

	for (int32 i = 0; i < NumLines; ++i)
	{
		TargetGain[i] = FMath::Pow(10.f, -3.f * DelaySeconds[i] / RT60);
	}
}

void FFeedbackDelayNetwork::SetDamping(float Amount)
{
	TargetDamping = FMath::Clamp(Amount, 0.0f, 0.95f);
}

void FFeedbackDelayNetwork::SetPredelaySeconds(float Seconds)
{
	const float Clamped = FMath::Clamp(Seconds, 0.f, MaxPredelaySeconds);
	const int32 Wanted = FMath::RoundToInt(Clamped * SampleRate);

	if (FMath::Abs(Wanted - PredelaySamples) * (1.f / SampleRate) > 0.005f)
	{
		PredelaySamples = FMath::Clamp(Wanted, 0, Predelay.Buffer.Num() - 1);
	}
}

void FFeedbackDelayNetwork::SetDecayFromBands(const float RT60PerBand[6])
{
	const float MidRT60 = 0.5f * (RT60PerBand[2] + RT60PerBand[3]);
	SetRT60(MidRT60);

	const float LowRT60  = 0.5f * (RT60PerBand[0] + RT60PerBand[1]);
	const float HighRT60 = 0.5f * (RT60PerBand[4] + RT60PerBand[5]);

	if (LowRT60 <= KINDA_SMALL_NUMBER || HighRT60 <= KINDA_SMALL_NUMBER)
	{
		SetDamping(0.f);
		return;
	}
	
	const float Ratio = FMath::Clamp(HighRT60 / LowRT60, 0.05f, 1.f);

	const float T = GetMeanDelaySeconds();
	const float MeanGain = FMath::Pow(10.f, -3.f * T / FMath::Max(MidRT60, KINDA_SMALL_NUMBER));
	const float G = FMath::Loge(FMath::Max(MeanGain, KINDA_SMALL_NUMBER));
	const float X = FMath::Exp(G * (1.f / Ratio - 1.f));

	SetDamping((1.f - X) / (1.f + X));
}

float FFeedbackDelayNetwork::GetMeanDelaySeconds() const
{
	float Sum = 0.f;
	for (int32 i = 0; i < NumLines; ++i) Sum += DelaySeconds[i];
	return Sum / float(NumLines);
}

void FFeedbackDelayNetwork::Reset()
{
	for (int32 i = 0; i < NumLines; ++i)
	{
		Lines[i].Reset();
		FilterZ1[i] = 0.f;
		Feed[i] = 0.f;
		Gain[i] = TargetGain[i];
	}
	Predelay.Reset();
	Damping = TargetDamping;
}

float FFeedbackDelayNetwork::ProcessSample(float In)
{
	const int32 BufLen = Predelay.Buffer.Num();
	Predelay.Buffer[Predelay.WriteIndex] = In;
	const int32 ReadIndex = (Predelay.WriteIndex + BufLen - PredelaySamples) % BufLen;
	const float Delayed = Predelay.Buffer[ReadIndex];
	if (++Predelay.WriteIndex >= BufLen) Predelay.WriteIndex = 0;

	TStaticArray<float, NumLines> Tap;
	for (int32 i = 0; i < NumLines; ++i)
	{
		Tap[i] = Lines[i].Process(Feed[i]);
	}

	float Sum = 0.f;
	for (int32 i = 0; i < NumLines; ++i) Sum += Tap[i];
	const float Out = Sum / FMath::Sqrt(float(NumLines));

	for (int32 i = 0; i < NumLines; ++i)
	{
		const float V = Tap[i] * Gain[i];
		FilterZ1[i] = (1.f - Damping) * V + Damping * FilterZ1[i];
		Tap[i] = FilterZ1[i];
	}

	FastHadamard(Tap);

	for (int32 i = 0; i < NumLines; ++i)
	{
		Feed[i] = Delayed + Tap[i];
	}

	return Out;
}

void FFeedbackDelayNetwork::ProcessBlock(const float* In, float* Out, int32 NumFrames)
{
	if (!In || !Out || NumFrames <= 0) return;

	const float InvFrames = 1.f / float(NumFrames);

	TStaticArray<float, NumLines> GainStep;
	for (int32 i = 0; i < NumLines; ++i)
	{
		GainStep[i] = (TargetGain[i] - Gain[i]) * InvFrames;
	}
	const float DampingStep = (TargetDamping - Damping) * InvFrames;

	for (int32 n = 0; n < NumFrames; ++n)
	{
		for (int32 i = 0; i < NumLines; ++i) Gain[i] += GainStep[i];
		Damping += DampingStep;

		Out[n] = ProcessSample(In[n]);
	}

	for (int32 i = 0; i < NumLines; ++i) Gain[i] = TargetGain[i];
	Damping = TargetDamping;
}

int32 FFeedbackDelayNetwork::NearestPrimeAtLeast(int32 Wanted)
{
	auto IsPrime = [](int32 N)
	{
		if (N < 2) return false;
		for (int32 D = 2; D * D <= N; ++D) if (N % D == 0) return false;
		return true;
	};

	int32 N = FMath::Max(Wanted, 2);
	while (!IsPrime(N)) ++N;
	return N;
}

void FFeedbackDelayNetwork::FastHadamard(TStaticArray<float, NumLines>& V)
{
	for (int32 Step = 1; Step < NumLines; Step *= 2)
	{
		for (int32 i = 0; i < NumLines; i += 2 * Step)
		{
			for (int32 j = i; j < i + Step; ++j)
			{
				const float a = V[j];
				const float b = V[j + Step];
				V[j]        = a + b;
				V[j + Step] = a - b;
			}
		}
	}

	const float Scale = 1.f / FMath::Sqrt(float(NumLines));
	for (int32 i = 0; i < NumLines; ++i) V[i] *= Scale;
}