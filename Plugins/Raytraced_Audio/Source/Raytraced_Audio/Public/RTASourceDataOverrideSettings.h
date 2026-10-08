#pragma once
#include "IAudioExtensionPlugin.h"
#include "RTASourceDataOverrideSettings.generated.h"

/**
 * Per-sound settings for the raytraced virtual source. Assign in the attenuation settings under
 * Source Data Override. Without an asset the defaults below are used.
 */
UCLASS()
class RAYTRACED_AUDIO_API URTASourceDataOverrideSettings : public USourceDataOverridePluginSourceSettingsBase
{
	GENERATED_BODY()
public:
	/** Move the sound to where it is heard from (a doorway, a corner). Off = keep it at its true position. */
	UPROPERTY(EditAnywhere, Category = "Virtual Source")
	bool bUseVirtualPosition = true;

	/** How fast the heard direction follows a new trace result, per second. 0 = no extra smoothing. */
	UPROPERTY(EditAnywhere, Category = "Virtual Source", meta = (ClampMin = "0.0"))
	float DirectionSmoothingSpeed = 12.f;
};
