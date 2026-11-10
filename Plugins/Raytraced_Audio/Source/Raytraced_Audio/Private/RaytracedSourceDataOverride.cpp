#include "RaytracedSourceDataOverride.h"
#include "RTASourceDataOverrideSettings.h"
#include "Audio.h"
#include "Log.h"

void FRaytracedSourceDataOverride::OnInitSource(const uint32 SourceId, const FName& /*AudioComponentUserId*/,
	USourceDataOverridePluginSourceSettingsBase* InSettings)
{
	FSourceState State;
	if (const URTASourceDataOverrideSettings* Settings = Cast<URTASourceDataOverrideSettings>(InSettings))
	{
		State.bUseVirtualPosition = Settings->bUseVirtualPosition;
		State.DirectionSmoothingSpeed = Settings->DirectionSmoothingSpeed;
	}

	// The occlusion plugin logs the same line; matching IDs confirm both plugins see the same voice.
	UE_LOG(LogRTA, Verbose, TEXT("VirtualSource OnInitSource id=%u"), SourceId);

	FScopeLock Lock(&SourceStatesLock);
	SourceStates.Add(SourceId, State);
}

void FRaytracedSourceDataOverride::OnReleaseSource(const uint32 SourceId)
{
	FScopeLock Lock(&SourceStatesLock);
	SourceStates.Remove(SourceId);
}

void FRaytracedSourceDataOverride::GetSourceDataOverrides(const uint32 SourceId,
	const FTransform& InListenerTransform, FWaveInstance* InOutWaveInstance)
{
	if (!InOutWaveInstance || !RTManager.IsValid()) return;

	// At this point Location is still the real emitter: report it before replacing it.
	const FVector TruePosition = InOutWaveInstance->Location;

	// False when occlusion isn't enabled for this sound (no trace, so nothing to move it to)
	// or the source hasn't registered yet. Location is then left alone.
	if (!RTManager->UpdateTrueEmitterPosition(SourceId, TruePosition)) return;

	if (!InOutWaveInstance->GetUseSpatialization()) return;

	FScopeLock Lock(&SourceStatesLock);
	FSourceState* State = SourceStates.Find(SourceId);
	if (!State || !State->bUseVirtualPosition) return;

	const FRaytraceManager::FSourceResult Result = RTManager->GetLatestResults(SourceId);
	if (!Result.bHasValidEstimate) return;

	const FVector NewDirection = Result.ArrivalDirection.GetSafeNormal();
	if (NewDirection.IsNearlyZero()) return;

	// Results arrive at the trace rate; this runs more often. Ease the direction in between.
	const double Now = FPlatformTime::Seconds();
	const float DeltaSeconds = State->LastUpdateSeconds > 0.0 ? float(Now - State->LastUpdateSeconds) : 0.f;
	State->LastUpdateSeconds = Now;

	if (State->SmoothedDirection.IsNearlyZero() || State->DirectionSmoothingSpeed <= 0.f)
	{
		State->SmoothedDirection = NewDirection;
	}
	else
	{
		const float Alpha = 1.f - FMath::Exp(-State->DirectionSmoothingSpeed * DeltaSeconds);
		const FVector Blended = FMath::Lerp(State->SmoothedDirection, NewDirection, Alpha);
		State->SmoothedDirection = Blended.IsNearlyZero() ? NewDirection : Blended.GetSafeNormal();
	}

	// Only the direction comes from the (slightly older) trace. Listener and distance are current,
	// so movement doesn't lag, and keeping the true distance leaves distance attenuation correct.
	const FVector ListenerPosition = InListenerTransform.GetLocation();
	InOutWaveInstance->Location =
		ListenerPosition + State->SmoothedDirection * FVector::Dist(ListenerPosition, TruePosition);
}
