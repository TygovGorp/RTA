#include "RaytracedReverb.h"

#include "Log.h"
#include "HAL/IConsoleManager.h"

namespace
{
	static int32 GRTARt60Source = 1;
	static FAutoConsoleVariableRef CVarRTARt60Source(
		TEXT("rta.RT60Source"),
		GRTARt60Source,
		TEXT("Reverb decay source: 0 = Eyring, 1 = measured T30 (falls back to T20, then Eyring)."),
		ECVF_Default);
	
	static float GRTAMaxRT60 = 8.f;
	static FAutoConsoleVariableRef CVarRTAMaxRT60(
		TEXT("rta.MaxRT60"),
		GRTAMaxRT60,
		TEXT("Upper clamp on the decay time fed to the reverb, in seconds."),
		ECVF_Default);

	static int32 GRTALogRT60 = 0;
	static FAutoConsoleVariableRef CVarRTALogRT60(
		TEXT("rta.LogRT60"),
		GRTALogRT60,
		TEXT("Log the decay actually driving the reverb, once per second."),
		ECVF_Default);
}
#include "RTAReverbSourceSettings.h"
#include "Sound/SoundSubmix.h"

void FRaytracedReverb::Initialize(const FAudioPluginInitializationParams InitializationParams)
{
	SampleRate = InitializationParams.SampleRate > 0 ? float(InitializationParams.SampleRate) : 48000.f;
	SourceStates.Empty(InitializationParams.NumSources);

	UE_LOG(LogRTA, Log, TEXT("RaytracedReverb: initialize, %.0f Hz, %d sources, %d frames/buffer"),
		SampleRate, InitializationParams.NumSources, InitializationParams.BufferLength);

	UE_LOG(LogRTA, Log, TEXT("Reverb settings class: %s"),
		*URTAReverbSourceSettings::StaticClass()->GetPathName());
}

void FRaytracedReverb::Shutdown()
{
	SourceStates.Empty();
	SubmixEffect.Reset();

	if (ReverbSubmix)
	{
		ReverbSubmix->RemoveFromRoot();
		ReverbSubmix = nullptr;
	}
	if (ReverbPreset)
	{
		ReverbPreset->RemoveFromRoot();
		ReverbPreset = nullptr;
	}

	UE_LOG(LogRTA, Log, TEXT("RaytracedReverb: shutdown"));
}

USoundSubmix* FRaytracedReverb::GetSubmix()
{
	if (ReverbSubmix)
	{
		return ReverbSubmix;
	}
	
	ReverbPreset = NewObject<URTAReverbSubmixPreset>(GetTransientPackage(), TEXT("RTAReverbSubmixPreset"));
	ReverbPreset->AddToRoot();

	ReverbSubmix = NewObject<USoundSubmix>(GetTransientPackage(), TEXT("RTAReverbSubmix"));
	ReverbSubmix->SubmixEffectChain.AddUnique(ReverbPreset);
	ReverbSubmix->bMuteWhenBackgrounded = true;
	ReverbSubmix->bAutoDisable = false;
	ReverbSubmix->AddToRoot();

	UE_LOG(LogRTA, Log, TEXT("RaytracedReverb: created submix and preset"));

	return ReverbSubmix;
}

FSoundEffectSubmixPtr FRaytracedReverb::GetEffectSubmix()
{
	if (SubmixEffect.IsValid())
	{
		return SubmixEffect;
	}

	GetSubmix();

	if (!ReverbPreset)
	{
		UE_LOG(LogRTA, Error, TEXT("RaytracedReverb: no preset, cannot create submix effect"));
		return nullptr;
	}

	FSoundEffectSubmixInitData InitData;
	InitData.SampleRate = SampleRate;

	SubmixEffect = USoundEffectPreset::CreateInstance<FSoundEffectSubmixInitData, FSoundEffectSubmix>(
		InitData, *ReverbPreset);

	if (SubmixEffect.IsValid())
	{
		SubmixEffect->SetEnabled(true);
		UE_LOG(LogRTA, Log, TEXT("RaytracedReverb: submix effect instance created"));
	}
	else
	{
		UE_LOG(LogRTA, Error, TEXT("RaytracedReverb: failed to create submix effect instance"));
	}

	return SubmixEffect;
}

void FRaytracedReverb::OnInitSource(const uint32 SourceId, const FName& /*AudioComponentUserId*/,
	const uint32 NumChannels, UReverbPluginSourceSettingsBase* InSettings)
{
	UE_LOG(LogRTA, Log, TEXT("Reverb OnInitSource id=%u channels=%u settings=%s"),
	   SourceId, NumChannels, InSettings ? *InSettings->GetName() : TEXT("NULL"));
	
	FReverbSourceState State;
	State.NumChannels = NumChannels;

	if (const URTAReverbSourceSettings* Settings = Cast<URTAReverbSourceSettings>(InSettings))
	{
		State.bEnableReverb = Settings->bEnableReverb;
		State.SendGain = Settings->SendGain;
	}
	
	SourceStates.Add(SourceId, State);
}

void FRaytracedReverb::OnReleaseSource(const uint32 SourceId)
{
	SourceStates.Remove(SourceId);
}

void FRaytracedReverb::UpdateRoomDecay()
{
	if (!RTManager.IsValid() || !SubmixEffect.IsValid())
	{
		return;
	}

	const FRaytraceManager::FRoomResult Room = RTManager->GetLatestRoomResult();
	if (!Room.bHasValidEstimate)
	{
		return;
	}

	float RT60[RTA::NumBands];
	for (int32 Band = 0; Band < RTA::NumBands; ++Band)
	{
		float Value = Room.EyringRT60[Band];
		if (GRTARt60Source != 0)
		{
			if (Room.bT30Valid[Band])      Value = Room.MeasuredT30[Band];
			else if (Room.bT20Valid[Band]) Value = Room.MeasuredT20[Band];
		}
		RT60[Band] = FMath::Clamp(Value, 0.05f, GRTAMaxRT60);
	}

	static_cast<FRTAReverbSubmix*>(SubmixEffect.Get())->SetRoomDecay(RT60, Room.FirstReflectionSeconds);

	if (GRTALogRT60 != 0)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - LastRT60LogSeconds > 1.0)
		{
			LastRT60LogSeconds = Now;
			UE_LOG(LogRTA, Log, TEXT("Reverb decay in use (source %d): [%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]"),
				GRTARt60Source, RT60[0], RT60[1], RT60[2], RT60[3], RT60[4], RT60[5]);
		}
	}
}

void FRaytracedReverb::ProcessSourceAudio(const FAudioPluginSourceInputData& InputData,
	FAudioPluginSourceOutputData& OutputData)
{
	static bool bLoggedOnce = false;
	if (!bLoggedOnce) { bLoggedOnce = true; UE_LOG(LogRTA, Log, TEXT("Reverb ProcessSourceAudio running")); }
	
	UpdateRoomDecay();

	const float* RESTRICT In = InputData.AudioBuffer->GetData();
	float* RESTRICT Out = OutputData.AudioBuffer.GetData();

	const int32 NumSamples = FMath::Min(InputData.AudioBuffer->Num(), OutputData.AudioBuffer.Num());

	if (!In || !Out || NumSamples <= 0)
	{
		return;
	}

	FReverbSourceState* State = SourceStates.Find(InputData.SourceId);

	if (State && !State->bEnableReverb)
	{
		FMemory::Memzero(Out, NumSamples * sizeof(float));
		State->PrevSend = 0.f;
		return;
	}

	float Energy = 1.f;
	if (RTManager.IsValid())
	{
		const FRaytraceManager::FSourceResult Src = RTManager->GetLatestResults(InputData.SourceId);
		if (Src.bHasValidEstimate)
		{
			float Sum = 0.f;
			for (int32 Band = 0; Band < RTA::NumBands; ++Band)
			{
				Sum += 1.f - Src.DirectTransmissionLoss[Band];
			}
			Energy = FMath::Clamp(Sum / float(RTA::NumBands), 0.f, 1.f);
		}
	}

	const float SendGain = State ? State->SendGain : 1.f;
	const float TargetSend = FMath::Sqrt(Energy) * SendGain;

	const float StartSend = (State && State->PrevSend >= 0.f) ? State->PrevSend : TargetSend;
	const float SendStep = (TargetSend - StartSend) / float(NumSamples);

	float Send = StartSend;
	for (int32 Sample = 0; Sample < NumSamples; ++Sample)
	{
		Out[Sample] = In[Sample] * Send;
		Send += SendStep;
	}

	if (State)
	{
		State->PrevSend = TargetSend;
	}
}