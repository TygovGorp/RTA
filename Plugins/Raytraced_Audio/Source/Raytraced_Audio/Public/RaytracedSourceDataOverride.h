#pragma once
#include "IAudioExtensionPlugin.h"
#include "RaytraceManager.h"

/**
 * Moves each sound to the raytracer's virtual position before the engine spatialises it.
 *
 * Unreal calls GetSourceDataOverrides per source before any other parameters are updated,
 * while the wave instance still holds the real emitter position. That position goes to the
 * tracer first, then the location is replaced by the virtual one, so panning and HRTF use
 * where the sound is heard from and occlusion keeps tracing from where it really is.
 */
class FRaytracedSourceDataOverride : public IAudioSourceDataOverride
{
public:
	explicit FRaytracedSourceDataOverride(const TSharedPtr<FRaytraceManager>& Manager) : RTManager(Manager)
	{}

	virtual void OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId,
		USourceDataOverridePluginSourceSettingsBase* InSettings) override;

	virtual void OnReleaseSource(const uint32 SourceId) override;

	virtual void GetSourceDataOverrides(const uint32 SourceId, const FTransform& InListenerTransform,
		FWaveInstance* InOutWaveInstance) override;

private:
	struct FSourceState
	{
		bool  bUseVirtualPosition = true;
		float DirectionSmoothingSpeed = 12.f;
		FVector SmoothedDirection = FVector::ZeroVector;
		double LastUpdateSeconds = 0.0;
	};

	TSharedPtr<FRaytraceManager> RTManager;
	TMap<uint32, FSourceState> SourceStates;
	FCriticalSection SourceStatesLock;
};
