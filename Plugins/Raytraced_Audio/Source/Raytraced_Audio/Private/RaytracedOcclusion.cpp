#include "RaytracedOcclusion.h"
#include "RTAOcclusionSourceSettings.h"

void FRaytracedOcclusion::Initialize(const FAudioPluginInitializationParams InitializationParams)
{
	SampleRate = InitializationParams.SampleRate;
	UE_LOG(LogTemp, Warning, TEXT("RTA: SampleRate=%f"), SampleRate);
}

void FRaytracedOcclusion::Shutdown()
{
}

void FRaytracedOcclusion::OnInitSource(const uint32 SourceId, const FName& /*AudioComponentUserId*/,
	const uint32 /*NumChannels*/, UOcclusionPluginSourceSettingsBase* InSettings)
{
	UE_LOG(LogTemp, Log, TEXT("RTA: Init Source"));
	
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
	
	const float Gain = 1.0f - Results.DirectTransmissionLoss; 

	const int32 NumSamples = InputData.AudioBuffer->Num();
	check(OutputData.AudioBuffer.Num() == NumSamples); 

	// One-pole lowpass driven by air absorption's distance-based cutoff.
	const float CutoffHz = FMath::Clamp(Results.DirectLowpassCutoffHz, 20.f, 20000.f);
	const float Alpha = 1.f - FMath::Exp(-2.f * PI * CutoffHz / SampleRate);

	FOcclusionFilterState* FilterState = nullptr;
	{
		FReadScopeLock Lock(FilterStatesLock);
		FilterState = FilterStates.Find(InputData.SourceId);
	}

	if (!FilterState)
	{
		// Shouldn't happen if OnInitSource ran first, but don't crash the audio thread over it.
		UE_LOG(LogTemp, Warning, TEXT("RTA: No filter state for SourceId=%u, applying gain only"), InputData.SourceId);
		for (int32 i = 0; i < NumSamples; ++i)
		{
			OutputData.AudioBuffer[i] = (*InputData.AudioBuffer)[i] * Gain;
		}
		return;
	}

	float Prev = FilterState->PrevOutput;
	for (int32 i = 0; i < NumSamples; ++i)
	{
		const float Input = (*InputData.AudioBuffer)[i] * Gain;
		Prev = Prev + Alpha * (Input - Prev);
		OutputData.AudioBuffer[i] = Prev;
	}
	FilterState->PrevOutput = Prev;
}