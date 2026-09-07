#pragma once
#include "IAudioExtensionPlugin.h"
#include "RTAOcclusionSourceSettings.generated.h"

UCLASS()
class RAYTRACED_AUDIO_API URTAOcclusionSourceSettings : public UOcclusionPluginSourceSettingsBase
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Air Absorption")
    float AirAbsorptionMinDistance = 300.f;

    UPROPERTY(EditAnywhere, Category="Air Absorption")
    float AirAbsorptionMaxDistance = 5000.f;

    UPROPERTY(EditAnywhere, Category="Air Absorption")
    float AirAbsorptionCutoffAtMinDistance = 20000.f;

    UPROPERTY(EditAnywhere, Category="Air Absorption")
    float AirAbsorptionCutoffAtMaxDistance = 2000.f;
};