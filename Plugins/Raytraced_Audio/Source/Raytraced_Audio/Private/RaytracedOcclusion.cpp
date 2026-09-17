#include "RaytracedOcclusion.h"
#include "RTAOcclusionSourceSettings.h"
#include "log.h"

void FRaytracedOcclusion::Initialize(const FAudioPluginInitializationParams InitializationParams)
{
	SampleRate = InitializationParams.SampleRate;
	UE_LOG(LogRTA, Warning, TEXT("SampleRate=%f"), SampleRate);
}

void FRaytracedOcclusion::Shutdown()
{
}

void FRaytracedOcclusion::OnInitSource(const uint32 SourceId, const FName& /*AudioComponentUserId*/,
	const uint32 /*NumChannels*/, UOcclusionPluginSourceSettingsBase* InSettings)
{
	UE_LOG(LogRTA, Log, TEXT("Init Source"));
	
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
    RTManager->UpdateListenerPosition(InputData.SourceId, InputData.SpatializationParams->ListenerPosition);
    auto Results = RTManager->GetLatestResults(InputData.SourceId);
	
    float GainBands[3];
	if (Results.bHasValidEstimate)
	{
		for (int32 Band = 0; Band < 3; ++Band)
			GainBands[Band] = 1.f - Results.DirectTransmissionLoss[Band];
	}
	else
	{
		for (int32 Band = 0; Band < 3; ++Band)
			GainBands[Band] = 0.f; 
	}

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
        for (int32 i = 0; i < NumSamples; ++i)
        {
            OutputData.AudioBuffer[i] = (*InputData.AudioBuffer)[i] * GainBands[1];
        }
        return;
    }
	
    constexpr float Crossover1Hz = 1000.f;  // sqrt(400 * 2500)
    constexpr float Crossover2Hz = 6124.f;  // sqrt(2500 * 15000)

    const float Alpha1 = 1.f - FMath::Exp(-2.f * PI * Crossover1Hz / SampleRate);
    const float Alpha2 = 1.f - FMath::Exp(-2.f * PI * Crossover2Hz / SampleRate);

    float PrevLow = FilterState->PrevLow;
    float PrevMidLP = FilterState->PrevMidLP;
    float PrevOutput = FilterState->PrevOutput;

    for (int32 i = 0; i < NumSamples; ++i)
    {
        const float Input = (*InputData.AudioBuffer)[i];
    	
        PrevLow = PrevLow + Alpha1 * (Input - PrevLow);          // low band
        const float Remainder1 = Input - PrevLow;                 // mid+high

        PrevMidLP = PrevMidLP + Alpha2 * (Remainder1 - PrevMidLP);// mid band
        const float High = Remainder1 - PrevMidLP;                // high band

        // Apply each band's material-driven gain, then recombine.
        const float Occluded = (PrevLow * GainBands[0])
                              + (PrevMidLP * GainBands[1])
                              + (High * GainBands[2]);
    	
        PrevOutput = PrevOutput + AirAbsorptionAlpha * (Occluded - PrevOutput);
        OutputData.AudioBuffer[i] = PrevOutput;
    }

    FilterState->PrevLow = PrevLow;
    FilterState->PrevMidLP = PrevMidLP;
    FilterState->PrevOutput = PrevOutput;
}