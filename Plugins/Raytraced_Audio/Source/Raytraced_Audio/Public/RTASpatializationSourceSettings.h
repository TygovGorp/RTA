#pragma once
#include "IAudioExtensionPlugin.h"
#include "RTASpatializationSourceSettings.generated.h"

UCLASS()
class RAYTRACED_AUDIO_API URTASpatializationSourceSettings : public USpatializationPluginSourceSettingsBase
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="temp")
    float temp = 0.f;
};