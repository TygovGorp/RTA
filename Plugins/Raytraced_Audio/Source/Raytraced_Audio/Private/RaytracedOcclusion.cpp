#include "RaytracedOcclusion.h"

void FRaytracedOcclusion::Initialize(const FAudioPluginInitializationParams /*InitializationParams*/)
{
	
}

void FRaytracedOcclusion::Shutdown()
{
}

void FRaytracedOcclusion::OnInitSource(const uint32 SourceId, const FName& /*AudioComponentUserId*/,
	const uint32 /*NumChannels*/, UOcclusionPluginSourceSettingsBase* /*InSettings*/)
{
	UE_LOG(LogTemp, Log, TEXT("RTA: Init Source"));
	RTManager->RegisterSource(SourceId);
}

void FRaytracedOcclusion::OnReleaseSource(const uint32 SourceId)
{
	RTManager->UnregisterSource(SourceId);
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

	for (int32 i = 0; i < NumSamples; ++i)
	{
		OutputData.AudioBuffer[i] = (*InputData.AudioBuffer)[i] * Gain;
	}
}
