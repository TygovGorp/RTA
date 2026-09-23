#include "RaytracedOcclusion.h"
#include "RTAOcclusionSourceSettings.h"
#include "Log.h"

void FRaytracedOcclusion::Initialize(const FAudioPluginInitializationParams InitializationParams)
{
	SampleRate = InitializationParams.SampleRate;
	UE_LOG(LogRTA, Log, TEXT("Occlusion initialised, SampleRate=%f"), SampleRate);
}

void FRaytracedOcclusion::Shutdown()
{
}

void FRaytracedOcclusion::OnInitSource(const uint32 SourceId, const FName& /*AudioComponentUserId*/,
	const uint32 /*NumChannels*/, UOcclusionPluginSourceSettingsBase* InSettings)
{
	if (const URTAOcclusionSourceSettings* RTASettings = Cast<URTAOcclusionSourceSettings>(InSettings))
	{
		RTManager->RegisterSource(SourceId,
			RTASettings->AirAbsorptionMinDistance,
			RTASettings->AirAbsorptionMaxDistance,
			RTASettings->AirAbsorptionCutoffAtMinDistance,
			RTASettings->AirAbsorptionCutoffAtMaxDistance);
	}
	else
	{
		RTManager->RegisterSource(SourceId);
	}

	FWriteScopeLock Lock(FilterStatesLock);
	FilterStates.Add(SourceId, FOcclusionFilterState());
}

void FRaytracedOcclusion::OnReleaseSource(const uint32 SourceId)
{
	RTManager->UnregisterSource(SourceId);

	FWriteScopeLock Lock(FilterStatesLock);
	FilterStates.Remove(SourceId);
}

void FRaytracedOcclusion::ProcessAudio(const FAudioPluginSourceInputData& InputData,
	FAudioPluginSourceOutputData& OutputData)
{
	RTManager->UpdateEmitterPosition(InputData.SourceId, InputData.SpatializationParams->EmitterWorldPosition);

	RTManager->UpdateListenerPosition(InputData.SpatializationParams->ListenerPosition);

	const FRaytraceManager::FSourceResult Results = RTManager->GetLatestResults(InputData.SourceId);

	float BandEnergy[RTA::NumBands];
	if (Results.bHasValidEstimate)
	{
		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			BandEnergy[Band] = 1.f - Results.DirectTransmissionLoss[Band];
		}
	}
	else
	{
		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			BandEnergy[Band] = 1.f;
		}
	}

	const float LowEnergy  = 0.5f * (BandEnergy[0] + BandEnergy[1]);  // 125 + 250 Hz
	const float MidEnergy  = 0.5f * (BandEnergy[2] + BandEnergy[3]);  // 500 Hz + 1 kHz
	const float HighEnergy = 0.5f * (BandEnergy[4] + BandEnergy[5]);  // 2 kHz + 4 kHz

	const float LowGain  = FMath::Sqrt(FMath::Max(LowEnergy,  0.f));
	const float MidGain  = FMath::Sqrt(FMath::Max(MidEnergy,  0.f));
	const float HighGain = FMath::Sqrt(FMath::Max(HighEnergy, 0.f));

	const int32 NumSamples = InputData.AudioBuffer->Num();
	check(OutputData.AudioBuffer.Num() == NumSamples);

	const float CutoffHz = FMath::Clamp(Results.DirectLowpassCutoffHz, 20.f, 20000.f);
	const float AirAbsorptionAlpha = 1.f - FMath::Exp(-2.f * PI * CutoffHz / SampleRate);

	FOcclusionFilterState* FilterState = nullptr;
	{
		FReadScopeLock Lock(FilterStatesLock);
		FilterState = FilterStates.Find(InputData.SourceId);
	}

	if (!FilterState)
	{
		UE_LOG(LogRTA, Warning, TEXT("No filter state for SourceId=%u, applying gain only"), InputData.SourceId);

		float MeanEnergy = 0.f;
		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			MeanEnergy += BandEnergy[Band];
		}
		MeanEnergy /= float(RTA::NumBands);

		const float BroadbandGain = FMath::Sqrt(FMath::Max(MeanEnergy, 0.f));

		for (int32 i = 0; i < NumSamples; ++i)
		{
			OutputData.AudioBuffer[i] = (*InputData.AudioBuffer)[i] * BroadbandGain;
		}
		return;
	}

	constexpr float Crossover1Hz = 354.f;   // low  / mid
	constexpr float Crossover2Hz = 1414.f;  // mid  / high

	const float Alpha1 = 1.f - FMath::Exp(-2.f * PI * Crossover1Hz / SampleRate);
	const float Alpha2 = 1.f - FMath::Exp(-2.f * PI * Crossover2Hz / SampleRate);

	const float PrevLowGain  = FilterState->bHasPrevGains ? FilterState->PrevLowGain  : LowGain;
	const float PrevMidGain  = FilterState->bHasPrevGains ? FilterState->PrevMidGain  : MidGain;
	const float PrevHighGain = FilterState->bHasPrevGains ? FilterState->PrevHighGain : HighGain;
	const float InvSamples = (NumSamples > 0) ? 1.f / float(NumSamples) : 0.f;

	float PrevLow = FilterState->PrevLow;
	float PrevMidLP = FilterState->PrevMidLP;
	float PrevOutput = FilterState->PrevOutput;

	for (int32 i = 0; i < NumSamples; ++i)
	{
		const float Input = (*InputData.AudioBuffer)[i];
		const float T = float(i) * InvSamples;

		PrevLow = PrevLow + Alpha1 * (Input - PrevLow);            // low band
		const float Remainder1 = Input - PrevLow;                  // mid + high

		PrevMidLP = PrevMidLP + Alpha2 * (Remainder1 - PrevMidLP); // mid band
		const float High = Remainder1 - PrevMidLP;                 // high band

		const float Occluded = (PrevLow   * FMath::Lerp(PrevLowGain,  LowGain,  T))
		                     + (PrevMidLP * FMath::Lerp(PrevMidGain,  MidGain,  T))
		                     + (High      * FMath::Lerp(PrevHighGain, HighGain, T));

		PrevOutput = PrevOutput + AirAbsorptionAlpha * (Occluded - PrevOutput);
		OutputData.AudioBuffer[i] = PrevOutput;
	}

	FilterState->PrevLow = PrevLow;
	FilterState->PrevMidLP = PrevMidLP;
	FilterState->PrevOutput = PrevOutput;
	FilterState->PrevLowGain = LowGain;
	FilterState->PrevMidGain = MidGain;
	FilterState->PrevHighGain = HighGain;
	FilterState->bHasPrevGains = true;
}
