#include "RTAReverbSubmix.h"
#include "Log.h"
#include "HAL/IConsoleManager.h"
#include "RTAAcousticBands.h"

void FRTAReverbSubmix::Init(const FSoundEffectSubmixInitData& InitData)
{
	FDN.Init(InitData.SampleRate);
	MonoIn.SetNumUninitialized(RTA::MaxFramesPerBlock);
	StereoOut[0].SetNumUninitialized(RTA::MaxFramesPerBlock);
	StereoOut[1].SetNumUninitialized(RTA::MaxFramesPerBlock);

	UE_LOG(LogRTA, Log, TEXT("RTAReverbSubmix: init at %.0f Hz"), InitData.SampleRate);
}

void FRTAReverbSubmix::OnPresetChanged()
{
	GET_EFFECT_SETTINGS(RTAReverbSubmix);
	OutputGain = Settings.OutputGain;
	
	if (!bRoomDecayApplied)
	{
		FDN.SetRT60(Settings.RT60Seconds);
		FDN.SetDamping(Settings.Damping);
	}
}

void FRTAReverbSubmix::SetRoomDecay(const float RT60PerBand[6], float PredelaySeconds,
                                    float DampingScale, float DampingOverride)
{
	//FDN.SetPredelaySeconds(PredelaySeconds);
	
	bool bRoomDelayAppliedChanged = !bRoomDecayApplied
		|| !FMath::IsNearlyEqual(DampingScale, LastDampingScale)
		|| !FMath::IsNearlyEqual(DampingOverride, LastDampingOverride);
	for (int32 Band = 0; Band < 6 && !bRoomDelayAppliedChanged; ++Band)
	{
		bRoomDelayAppliedChanged = FMath::Abs(RT60PerBand[Band] - LastAppliedRT60[Band])
			> 0.02f * FMath::Max(LastAppliedRT60[Band], 0.05f);
	}

	if (!bRoomDelayAppliedChanged)
	{
		return;
	}

	FDN.SetDecayFromBands(RT60PerBand);
	
	if (DampingOverride >= 0.f)
	{
		FDN.SetDamping(DampingOverride);
	}
	else if (!FMath::IsNearlyEqual(DampingScale, 1.f))
	{
		FDN.SetDamping(FDN.GetDamping() * DampingScale);
	}

	FMemory::Memcpy(LastAppliedRT60, RT60PerBand, sizeof(LastAppliedRT60));
	LastDampingScale = DampingScale;
	LastDampingOverride = DampingOverride;
	bRoomDecayApplied = true;
}

void FRTAReverbSubmix::OnProcessAudio(const FSoundEffectSubmixInputData& InputData,
                                      FSoundEffectSubmixOutputData& OutputData)
{
	static bool bOnce = false;
	if (!bOnce)
	{
		bOnce = true;
		UE_LOG(LogRTA, Log, TEXT("RTAReverbSubmix: processing, %d frames %d ch"),
			InputData.NumFrames, InputData.NumChannels);
	}
	
	const float* RESTRICT In  = InputData.AudioBuffer->GetData();
	float* RESTRICT Out = OutputData.AudioBuffer->GetData();
	
	const int32 N = FMath::Min(InputData.NumFrames, MonoIn.Num());
	const int32 C = FMath::Max(InputData.NumChannels, 1);

	if (InputData.NumFrames > MonoIn.Num())
	{
		UE_LOG(LogRTA, Error, TEXT("RTAReverbSubmix: %d frames requested, scratch holds %d; block truncated"),
			InputData.NumFrames, MonoIn.Num());
	}
	
	for (int i = 0; i < N; ++i)
	{
		float Acc = 0.f;
		for (int32 Ch = 0; Ch < C; ++Ch)
			Acc += In[i*C + Ch];
		MonoIn[i] = Acc / static_cast<float>(C);
	}

	FDN.ProcessBlock(MonoIn.GetData(), {StereoOut[0].GetData(), StereoOut[1].GetData()}, N);

	for (int32 i = 0; i < N; ++i)
	{
		for (int32 Ch = 0; Ch < C; ++Ch)
		{
			Out[i*C + Ch] = StereoOut[Ch & 1][i] * OutputGain;
		}
	}
}