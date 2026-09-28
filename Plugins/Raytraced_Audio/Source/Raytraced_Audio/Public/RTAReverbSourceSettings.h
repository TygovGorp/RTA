#pragma once
#include "IAudioExtensionPlugin.h"
#include "RTAReverbSourceSettings.generated.h"

UCLASS()
class RAYTRACED_AUDIO_API URTAReverbSourceSettings : public UReverbPluginSourceSettingsBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Reverb")
	bool bEnableReverb = true;

	UPROPERTY(EditAnywhere, Category = "Reverb", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float SendGain = 1.f;
};
