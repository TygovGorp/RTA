#pragma once

#include "CoreMinimal.h"
#include "FeedbackDelayNetwork.h"
#include "Sound/SoundEffectSubmix.h"
#include "RTAReverbSubmix.generated.h"

USTRUCT(BlueprintType)
struct RAYTRACED_AUDIO_API FRTAReverbSubmixSettings
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTA Reverb", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float OutputGain = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTA Reverb", meta = (ClampMin = "0.0"))
	float RT60Seconds = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTA Reverb", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float Damping = 0.2f;
};

class RAYTRACED_AUDIO_API FRTAReverbSubmix : public FSoundEffectSubmix
{
public:
	
	virtual void OnPresetChanged() override;
	virtual void OnProcessAudio(const FSoundEffectSubmixInputData& InputData,
								FSoundEffectSubmixOutputData& OutputData) override;
	
	void SetRoomDecay(const float RT60PerBand[6], float PredelaySeconds,
					  float DampingScale, float DampingOverride);

private:
	virtual void Init(const FSoundEffectSubmixInitData& InitData) override;
	
	FFeedbackDelayNetwork FDN;
	float OutputGain = 1.f;
	float LastAppliedRT60[6] = {};
	float LastDampingScale = 1.f;
	float LastDampingOverride = -1.f;
	bool  bRoomDecayApplied = false;
	Audio::AlignedFloatBuffer MonoIn;
	Audio::AlignedFloatBuffer MonoOut;
};

UCLASS()
class RAYTRACED_AUDIO_API URTAReverbSubmixPreset : public USoundEffectSubmixPreset
{
	GENERATED_BODY()
public:
	EFFECT_PRESET_METHODS(RTAReverbSubmix)

	UPROPERTY(EditAnywhere, Category = "RTA Reverb", meta = (ShowOnlyInnerProperties))
	FRTAReverbSubmixSettings Settings;
};