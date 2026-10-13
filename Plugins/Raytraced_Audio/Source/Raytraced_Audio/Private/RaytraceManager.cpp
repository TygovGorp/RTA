#include "RaytraceManager.h"

#include "AcousticMaterialAsset.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Log.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Tasks/Task.h"
#include "EchogramXlsx.h"
#include "RTADecayMetrics.h"
#include "Misc/DateTime.h"
#include "Async/ParallelFor.h"
#include "BatchedTracer.h"
#include "HAL/IConsoleManager.h"
#include "DrawDebugHelpers.h"
#include "Components/PrimitiveComponent.h"

namespace
{
	static int32 GRTAValidateBatchedTrace = 0;
	static FAutoConsoleVariableRef CVarRTAValidateBatchedTrace(
		TEXT("rta.ValidateBatchedTrace"), GRTAValidateBatchedTrace,
		TEXT("Compare the batched tracer against UWorld::LineTraceTestByChannel on a sample."),
		ECVF_Default);
	
	static float GRTAProbeShadowThreshold = 1e-5f;
	static FAutoConsoleVariableRef CVarRTAProbeShadowThreshold(
		TEXT("rta.ProbeShadowThreshold"), GRTAProbeShadowThreshold,
		TEXT("Room probe: expected deposit below which shadow rays are rouletted. 0 = always trace."),
		ECVF_Default);
	
	static int32 GRTAProbeParallel = 1;
	static FAutoConsoleVariableRef CVarRTAProbeParallel(
		TEXT("rta.ProbeParallel"), GRTAProbeParallel,
		TEXT("Room probe: 1 = ParallelFor, 0 = single thread."),
		ECVF_Default);

	static int32 GRTAValidateProbeTrace = 0;
	static FAutoConsoleVariableRef CVarRTAValidateProbeTrace(
		TEXT("rta.ValidateProbeTrace"), GRTAValidateProbeTrace,
		TEXT("Compare the probe's batched shadow rays against the engine path on a sample."),
		ECVF_Default);

	constexpr float ShadowRouletteMinP = 0.05f;

	static int32 GRTAShoebox = 0;
	static FAutoConsoleVariableRef CVarRTAShoebox(
		TEXT("rta.Shoebox"), GRTAShoebox,
		TEXT("1 = print a validation report against analytic predictions every probe."),
		ECVF_Default);

	static float GRTAShoeboxX = 2.f;
	static float GRTAShoeboxY = 2.f;
	static float GRTAShoeboxZ = 2.f;
	static float GRTAShoeboxAlpha = 0.2f;
	static FAutoConsoleVariableRef CVarRTAShoeboxX(TEXT("rta.Shoebox.X"), GRTAShoeboxX,
		TEXT("Shoebox inner size along X, metres."), ECVF_Default);
	static FAutoConsoleVariableRef CVarRTAShoeboxY(TEXT("rta.Shoebox.Y"), GRTAShoeboxY,
		TEXT("Shoebox inner size along Y, metres."), ECVF_Default);
	static FAutoConsoleVariableRef CVarRTAShoeboxZ(TEXT("rta.Shoebox.Z"), GRTAShoeboxZ,
		TEXT("Shoebox inner size along Z, metres."), ECVF_Default);
	static FAutoConsoleVariableRef CVarRTAShoeboxAlpha(TEXT("rta.Shoebox.Alpha"), GRTAShoeboxAlpha,
		TEXT("Uniform absorption of every shoebox surface."), ECVF_Default);

	constexpr double ShoeboxMfpTolerancePct = 2.0;
	constexpr double ShoeboxT30TolerancePct = 10.0;
	constexpr float  ShoeboxAlphaTolerance  = 0.005f;

	static int32 GRTADebugDrawArrival = 0;
	static FAutoConsoleVariableRef CVarRTADebugDrawArrival(
		TEXT("rta.DebugDrawArrival"), GRTADebugDrawArrival,
		TEXT("Draw each source's true direction (red), apparent direction (green: thick up to the first wall, thin beyond it) ")
		TEXT("and the corner or door the sound comes from (magenta)."),
		ECVF_Default);

	static int32 GRTADebugDrawVirtualPosition = 0;
	static FAutoConsoleVariableRef CVarRTADebugDrawVirtualPosition(
		TEXT("rta.DebugDrawVirtualPosition"), GRTADebugDrawVirtualPosition,
		TEXT("Draw each source's published virtual position (cyan) with an arrow from the real emitter."),
		ECVF_Default);

	static float GRTAProbeRouletteEnergy = 1e-5f;
	static FAutoConsoleVariableRef CVarRTAProbeRouletteEnergy(
		TEXT("rta.ProbeRouletteEnergy"), GRTAProbeRouletteEnergy,
		TEXT("Room probe: ray energy below which roulette begins. Lower = longer, costlier tail."),
		ECVF_Default);

	static int32 GRTADiffraction = 1;
	static FAutoConsoleVariableRef CVarRTADiffraction(
		TEXT("rta.Diffraction"), GRTADiffraction,
		TEXT("1 = diffraction around the corner found on the bounce-ray routes adds to the level. 0 = off."),
		ECVF_Default);

	constexpr float SpeedOfSoundMps     = 343.f;

	static int32 GRTAReflectionSpreading = 1;
	static FAutoConsoleVariableRef CVarRTAReflectionSpreading(
		TEXT("rta.ReflectionSpreading"), GRTAReflectionSpreading,
		TEXT("1 = scale each connected reflection path by (straight distance / path length)^2, ")
		TEXT("so reflections and diffraction are both relative to an open path. 0 = off."),
		ECVF_Default);

	static float GRTAPrecedenceMs = 10.f;
	static FAutoConsoleVariableRef CVarRTAPrecedenceMs(
		TEXT("rta.PrecedenceMs"), GRTAPrecedenceMs,
		TEXT("Arrival-time window for the direction weighting, in ms. Later arrivals count less ")
		TEXT("for direction (precedence effect); the level is unaffected. 0 = off."),
		ECVF_Default);

	static int32 GRTAArrivalMode = 1;
	static FAutoConsoleVariableRef CVarRTAArrivalMode(
		TEXT("rta.ArrivalMode"), GRTAArrivalMode,
		TEXT("0 = blend all arrival directions (old behaviour). ")
		TEXT("1 = pick one route: the first arrival, unless a later route is much louder (precedence rule)."),
		ECVF_Default);

	static float GRTAPrecedenceDb = 12.f;
	static FAutoConsoleVariableRef CVarRTAPrecedenceDb(
		TEXT("rta.PrecedenceDb"), GRTAPrecedenceDb,
		TEXT("ArrivalMode 1: a later route must be this many dB louder than the current pick to take over the direction."),
		ECVF_Default);

	static float GRTAPrecedenceHystDb = 3.f;
	static FAutoConsoleVariableRef CVarRTAPrecedenceHystDb(
		TEXT("rta.PrecedenceHystDb"), GRTAPrecedenceHystDb,
		TEXT("ArrivalMode 1: bonus in dB for last update's route, so the direction doesn't flip between close routes."),
		ECVF_Default);

	static int32 GRTACornerCandidates = 4;
	static FAutoConsoleVariableRef CVarRTACornerCandidates(
		TEXT("rta.CornerCandidates"), GRTACornerCandidates,
		TEXT("ArrivalMode 1: strongest arrival lobes per source whose shortest path is searched for its first corner."),
		ECVF_Default);

	static float GRTAArrivalFastAlpha = 0.6f;
	static FAutoConsoleVariableRef CVarRTAArrivalFastAlpha(
		TEXT("rta.ArrivalFastAlpha"), GRTAArrivalFastAlpha,
		TEXT("ArrivalMode 1: direction smoothing per update for noise-free routes (direct, through) and when the ")
		TEXT("route switches. Corner routes keep the normal smoothing, which hides their noise."),
		ECVF_Default);

	static int32 GRTARayTransmission = 1;
	static FAutoConsoleVariableRef CVarRTARayTransmission(
		TEXT("rta.RayTransmission"), GRTARayTransmission,
		TEXT("1 = occlusion bounce rays can pass through surfaces with an acoustic material (doors, windows, thin walls), ")
		TEXT("so routes through them are found anywhere, not only on the straight line. 0 = rays only reflect."),
		ECVF_Default);

	static float GRTARayTransmitMinDb = -40.f;
	static FAutoConsoleVariableRef CVarRTARayTransmitMinDb(
		TEXT("rta.RayTransmitMinDb"), GRTARayTransmitMinDb,
		TEXT("Surfaces letting through at least this much (dB, mean over bands) pass at least rta.RayTransmitMinProb of ")
		TEXT("the rays that hit them. The energy is reweighted, so the level stays correct; this only reduces noise."),
		ECVF_Default);

	static float GRTARayTransmitMinProb = 0.25f;
	static FAutoConsoleVariableRef CVarRTARayTransmitMinProb(
		TEXT("rta.RayTransmitMinProb"), GRTARayTransmitMinProb,
		TEXT("Minimum chance a ray passes through a surface above rta.RayTransmitMinDb."),
		ECVF_Default);

	static float GRTADoorMaxSizeCm = 1000.f;
	static FAutoConsoleVariableRef CVarRTADoorMaxSizeCm(
		TEXT("rta.DoorMaxSizeCm"), GRTADoorMaxSizeCm,
		TEXT("A transmissive surface no larger than this (cm) is treated as a door or window: the sound appears from its centre."),
		ECVF_Default);

	constexpr float MaxTransmitProb = 0.95f;
	constexpr float SurfaceAnchorPullbackCm = 5.f;

	constexpr int32 MaxTransmissionLayers = 4;
	constexpr int32 CornerBisectSteps  = 8;
	constexpr float CandidateMergeDeg  = 5.f;
	constexpr float HysteresisMatchDeg = 10.f;

	const TCHAR* ArrivalKindName(uint8 Kind)
	{
		switch (static_cast<FRaytraceManager::EArrivalKind>(Kind))
		{
		case FRaytraceManager::EArrivalKind::Direct:  return TEXT("direct");
		case FRaytraceManager::EArrivalKind::Through: return TEXT("through");
		case FRaytraceManager::EArrivalKind::Corner:  return TEXT("corner");
		case FRaytraceManager::EArrivalKind::Surface: return TEXT("surface");
		case FRaytraceManager::EArrivalKind::Blend:   return TEXT("blend");
		case FRaytraceManager::EArrivalKind::Hold:    return TEXT("hold");
		default:                                      return TEXT("?");
		}
	}

	float PrecedenceWindowMetres()
	{
		return GRTAPrecedenceMs > 0.f ? SpeedOfSoundMps * GRTAPrecedenceMs * 0.001f : 0.f;
	}

	float PrecedenceWeight(float ExtraPathMetres, float WindowMetres)
	{
		if (WindowMetres <= 0.f) return 1.f;
		return FMath::Exp(-FMath::Max(ExtraPathMetres, 0.f) / WindowMetres);
	}
}


FRaytraceManager::FRaytraceManager() : World(nullptr)
{
	UE_LOG(LogRTA, Log, TEXT("Create New Manager"));

	Listener = MakeShared<FListenerData>();

	AudioTraceDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FRaytraceManager::TickAudioTrace),
		AudioTraceTickInterval);

	RoomProbeDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FRaytraceManager::TickRoomProbe),
		RoomProbeTickInterval);
}

FRaytraceManager::~FRaytraceManager()
{
	FTSTicker::GetCoreTicker().RemoveTicker(AudioTraceDelegateHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(RoomProbeDelegateHandle);
}

void FRaytraceManager::RegisterSource(uint32 SourceId,
                                      float AirAbsorptionMinDistance,
                                      float AirAbsorptionMaxDistance,
                                      float AirAbsorptionCutoffAtMinDistance,
                                      float AirAbsorptionCutoffAtMaxDistance)
{
	TSharedPtr<FSourceRayData> NewData = MakeShared<FSourceRayData>();
	NewData->AirAbsorptionMinDistance = AirAbsorptionMinDistance;
	NewData->AirAbsorptionMaxDistance = AirAbsorptionMaxDistance;
	NewData->AirAbsorptionCutoffAtMinDistance = AirAbsorptionCutoffAtMinDistance;
	NewData->AirAbsorptionCutoffAtMaxDistance = AirAbsorptionCutoffAtMaxDistance;

	FWriteScopeLock Lock(ResultsLock);
	if (Results.Find(SourceId) != nullptr)
	{
		UE_LOG(LogRTA, Warning, TEXT("SourceId %u already present in Results map"), SourceId);
	}
	Results.Add(SourceId, NewData);
}

void FRaytraceManager::UnregisterSource(uint32 SourceId)
{
	FWriteScopeLock Lock(ResultsLock);
	Results.Remove(SourceId);
}

void FRaytraceManager::UpdateEmitterPosition(uint32 SourceId, const FVector& Position) const
{
	TSharedPtr<FSourceRayData> RayData = FindSourceRayData(SourceId);
	if (!RayData.IsValid()) return;

	FWriteScopeLock Lock(RayData->Lock);
	
	if (RayData->bTruePositionOverridden) return;

	ApplyEmitterPositionLocked(*RayData, Position);
}

bool FRaytraceManager::UpdateTrueEmitterPosition(uint32 SourceId, const FVector& Position) const
{
	TSharedPtr<FSourceRayData> RayData = FindSourceRayData(SourceId);
	if (!RayData.IsValid()) return false;

	FWriteScopeLock Lock(RayData->Lock);
	RayData->bTruePositionOverridden = true;
	ApplyEmitterPositionLocked(*RayData, Position);
	return true;
}

void FRaytraceManager::UpdateListenerPosition(const FVector& Position) const
{
	if (!Listener.IsValid()) return;

	FWriteScopeLock Lock(Listener->Lock);

	const bool bFirstSet = !Listener->bPositionSet;
	const float DistSqCm = bFirstSet ? 0.f : FVector::DistSquared(Listener->Position, Position);

	if (!bFirstSet && DistSqCm > FMath::Square(RTA::TeleportThresholdCm))
	{
		Listener->AccumulatedEchogram.Reset();
		Listener->ProbeCount = 0;
		Listener->bHasValidEstimate = false;
		Listener->Result.Write(FRoomResult());
		UE_LOG(LogRTA, Log, TEXT("Listener teleported; echogram reset."));
	}

	if (bFirstSet || DistSqCm > FMath::Square(MinPositionDeltaForDirty))
	{
		Listener->Position = Position;
		Listener->bPositionSet = true;
		Listener->bDirty = true;
	}
}

FRaytraceManager::FSourceResult FRaytraceManager::GetLatestResults(uint32 SourceId) const
{
	FSourceResult Out;
	TSharedPtr<FSourceRayData> RayData = FindSourceRayData(SourceId);
	if (RayData.IsValid())
	{
		if (RayData->Result.TryRead(Out) && Out.bHasAnchor)
		{
			FVector EmitterNow;
			bool bEmitterSet;
			{
				FReadScopeLock Lock(RayData->Lock);
				EmitterNow = RayData->EmitterPosition;
				bEmitterSet = RayData->bEmitterPositionSet;
			}

			FVector ListenerNow = FVector::ZeroVector;
			bool bListenerSet = false;
			if (Listener.IsValid())
			{
				FReadScopeLock Lock(Listener->Lock);
				ListenerNow = Listener->Position;
				bListenerSet = Listener->bPositionSet;
			}

			if (bEmitterSet && bListenerSet)
			{
				ApplyAnchor(Out, ListenerNow, EmitterNow);
			}
		}
	}
	return Out;
}

void FRaytraceManager::ApplyAnchor(FSourceResult& Result, const FVector& ListenerNow, const FVector& EmitterNow)
{
	const FVector ToAnchor = Result.ArrivalAnchor - ListenerNow;
	if (ToAnchor.SizeSquared() < 1.0) return;

	Result.ArrivalDirection = ToAnchor.GetUnsafeNormal();
	Result.VirtualPosition = ListenerNow + Result.ArrivalDirection * FVector::Dist(ListenerNow, EmitterNow);
}

FRaytraceManager::FRoomResult FRaytraceManager::GetLatestRoomResult() const
{
	FRoomResult Out;
	if (Listener.IsValid())
	{
		Listener->Result.TryRead(Out);
	}
	return Out;
}

void FRaytraceManager::DumpEchogram() const
{
	if (!Listener.IsValid()) return;
	FEchogram Snapshot;
	int64 ProbeCount;
	{
		FReadScopeLock Lock(Listener->Lock);
		Snapshot = Listener->AccumulatedEchogram;
		ProbeCount = Listener->ProbeCount;
	}
	
	const FDateTime Now = FDateTime::Now();
	const FString Path = FPaths::ProjectSavedDir()
		/ FString::Printf(TEXT("RTA_Echogram_%s.xlsx"), *Now.ToString(TEXT("%Y%m%d_%H%M%S")));
	const FString Note = FString::Printf(TEXT("Dumped %s, %lld probes accumulated."),
	                                     *Now.ToString(TEXT("%Y-%m-%d %H:%M:%S")), ProbeCount);

	int32 NonFinite = 0;
	if (RTA::WriteEchogramXlsx(Path, Snapshot, Note, NonFinite))
	{
		UE_LOG(LogRTA, Log, TEXT("Echogram written to %s"), *FPaths::ConvertRelativePathToFull(Path));
	}
	else
	{
		UE_LOG(LogRTA, Warning, TEXT("Failed to write echogram to %s"), *Path);
	}
	if (NonFinite > 0)
	{
		UE_LOG(LogRTA, Warning, TEXT("Echogram contained %d non-finite values (written as 0)."), NonFinite);
	}
}

bool FRaytraceManager::TickAudioTrace(float /*DeltaTime*/)
{
	DrawArrivalDebug();
	DrawVirtualPositionDebug();

	bool bExpected = false;
	if (!bAudioTraceRunning.compare_exchange_strong(bExpected, true))
	{
		return true;
	}

	bool bListenerDirty = false;
	bool bListenerReady = false;
	if (Listener.IsValid())
	{
		FWriteScopeLock Lock(Listener->Lock);
		bListenerReady = Listener->bPositionSet;
		bListenerDirty = Listener->bDirty;
		Listener->bDirty = false;
	}

	TArray<uint32> SourceIdsToTrace;
	if (bListenerReady)
	{
		FReadScopeLock Lock(ResultsLock);
		for (const TTuple<uint32, TSharedPtr<FSourceRayData>>& Result : Results)
		{
			FWriteScopeLock SourceLock(Result.Value->Lock);
			if ((Result.Value->bDirty || bListenerDirty)
				&& Result.Value->bEmitterPositionSet
				&& !Result.Value->bTraceInFlight)
			{
				Result.Value->bDirty = false;
				Result.Value->bTraceInFlight = true;
				SourceIdsToTrace.Add(Result.Key);
			}
		}
	}

	if (SourceIdsToTrace.Num() == 0)
	{
		bAudioTraceRunning.store(false);
		return true;
	}

	const uint32 TraceSeed = static_cast<uint32>(TraceCounter.Add(1));

	TWeakPtr<FRaytraceManager, ESPMode::ThreadSafe> WeakSelf = AsWeak();
	UE::Tasks::Launch(TEXT("RTA_AudioTrace"), [WeakSelf, SourceIdsToTrace, TraceSeed]()
	{
		if (TSharedPtr<FRaytraceManager, ESPMode::ThreadSafe> Self = WeakSelf.Pin())
		{
			Self->RunAudioTrace(SourceIdsToTrace, TraceSeed);
		}
	}, LowLevelTasks::ETaskPriority::BackgroundLow);

	return true;
}

void FRaytraceManager::DrawArrivalDebug() const
{
#if ENABLE_DRAW_DEBUG
	if (GRTADebugDrawArrival == 0) return;

	UWorld* DrawWorld = World.Get();
	if (!DrawWorld || !Listener.IsValid()) return;

	FVector ListenerPos;
	{
		FReadScopeLock Lock(Listener->Lock);
		if (!Listener->bPositionSet) return;
		ListenerPos = Listener->Position;
	}

	const FVector Origin = ListenerPos - FVector(0.f, 0.f, 15.f);
	const float LifeTime = AudioTraceTickInterval * 1.5f;

	TArray<TPair<FVector, FSourceResult>> Snapshot;
	{
		FReadScopeLock Lock(ResultsLock);
		Snapshot.Reserve(Results.Num());

		for (const TTuple<uint32, TSharedPtr<FSourceRayData>>& Entry : Results)
		{
			FVector EmitterPos;
			bool bPositionSet = false;
			{
				FReadScopeLock SourceLock(Entry.Value->Lock);
				bPositionSet = Entry.Value->bEmitterPositionSet;
				EmitterPos = Entry.Value->EmitterPosition;
			}
			if (!bPositionSet) continue;

			FSourceResult Result;
			if (!Entry.Value->Result.TryRead(Result) || !Result.bHasValidEstimate) continue;
			if (Result.bHasAnchor) ApplyAnchor(Result, ListenerPos, EmitterPos);

			Snapshot.Emplace(EmitterPos, Result);
		}
	}

	for (const TPair<FVector, FSourceResult>& Item : Snapshot)
	{
		const FVector& EmitterPos = Item.Key;
		const FSourceResult& Result = Item.Value;
		const float Distance = FVector::Dist(ListenerPos, EmitterPos);
		const FVector Apparent = Origin + Result.ArrivalDirection * Distance;

		// Where the source really is
		DrawDebugLine(DrawWorld, Origin, EmitterPos, FColor::Red, false, LifeTime, 0, 1.f);

		{
			FCollisionQueryParams DebugParams;
			DebugParams.bTraceComplex = true;
			FHitResult DebugHit;
			const bool bHitWall = DrawWorld->LineTraceSingleByChannel(DebugHit, Origin, Apparent, ECC_Visibility, DebugParams);
			const FVector VisibleEnd = bHitWall ? FVector(DebugHit.ImpactPoint) : Apparent;

			DrawDebugLine(DrawWorld, Origin, VisibleEnd, FColor::Green, false, LifeTime, 0, 3.f);
			if (bHitWall)
			{
				DrawDebugLine(DrawWorld, VisibleEnd, Apparent, FColor(0, 120, 0), false, LifeTime, 0, 0.5f);
			}
		}

		// Sphere sized by spread
		const float Radius = FMath::Lerp(10.f, 80.f, 1.f - Result.ArrivalFocus);
		DrawDebugSphere(DrawWorld, Apparent, Radius, 12, FColor::Green, false, LifeTime, 0, 1.f);

		DrawDebugString(DrawWorld, Apparent + FVector(0.f, 0.f, Radius + 10.f),
		                FString::Printf(TEXT("%s  focus %.2f"), ArrivalKindName(Result.ArrivalKind), Result.ArrivalFocus),
		                nullptr, FColor::White, LifeTime);

		// First corner of the chosen ray route
		if (Result.bHasCorner)
		{
			DrawDebugLine(DrawWorld, ListenerPos, Result.CornerPoint, FColor::Magenta, false, LifeTime, 0, 2.f);
			DrawDebugPoint(DrawWorld, Result.CornerPoint, 10.f, FColor::Magenta, false, LifeTime);
		}

	}
#endif
}

void FRaytraceManager::DrawVirtualPositionDebug() const
{
#if ENABLE_DRAW_DEBUG
	if (GRTADebugDrawVirtualPosition == 0) return;

	UWorld* DrawWorld = World.Get();
	if (!DrawWorld) return;

	FVector ListenerNow = FVector::ZeroVector;
	bool bListenerSet = false;
	if (Listener.IsValid())
	{
		FReadScopeLock Lock(Listener->Lock);
		ListenerNow = Listener->Position;
		bListenerSet = Listener->bPositionSet;
	}

	constexpr float LifeTime = AudioTraceTickInterval * 1.5f;

	struct FItem
	{
		uint32 SourceId;
		FVector EmitterPos;
		FSourceResult Result;
	};

	TArray<FItem> Items;
	{
		FReadScopeLock Lock(ResultsLock);
		Items.Reserve(Results.Num());

		for (const TTuple<uint32, TSharedPtr<FSourceRayData>>& Entry : Results)
		{
			FVector EmitterPos;
			{
				FReadScopeLock SourceLock(Entry.Value->Lock);
				if (!Entry.Value->bEmitterPositionSet) continue;
				EmitterPos = Entry.Value->EmitterPosition;
			}

			FSourceResult Result;
			if (!Entry.Value->Result.TryRead(Result) || !Result.bHasValidEstimate) continue;
			if (Result.bHasAnchor && bListenerSet) ApplyAnchor(Result, ListenerNow, EmitterPos);

			Items.Add({ .SourceId = Entry.Key, .EmitterPos = EmitterPos, .Result = Result });
		}
	}

	for (const FItem& Item : Items)
	{
		const FVector& Virtual = Item.Result.VirtualPosition;
		const float OffsetCm = FVector::Dist(Item.EmitterPos, Virtual);

		DrawDebugSphere(DrawWorld, Virtual, 25.f, 12, FColor::Cyan, false, LifeTime, 0, 2.f);
		DrawDebugPoint(DrawWorld, Virtual, 10.f, FColor::Cyan, false, LifeTime);

		DrawDebugBox(DrawWorld, Item.EmitterPos, FVector(10.f), FColor::Red, false, LifeTime, 0, 1.f);
		if (OffsetCm > 1.f)
		{
			DrawDebugDirectionalArrow(DrawWorld, Item.EmitterPos, Virtual, 30.f,
			                          FColor::Cyan, false, LifeTime, 0, 1.f);
		}

		DrawDebugString(DrawWorld, Virtual + FVector(0.f, 0.f, 45.f),
		                FString::Printf(TEXT("src %u virtual\noffset %.0f cm  focus %.2f%s"),
		                                Item.SourceId, OffsetCm, Item.Result.ArrivalFocus,
		                                Item.Result.bHasCorner ? TEXT("  corner") : TEXT("")),
		                nullptr, FColor::Cyan, LifeTime);
	}
#endif
}

void FRaytraceManager::RunAudioTrace(const TArray<uint32>& SourceIds, uint32 TraceSeed)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RTA_RunAudioTrace);

	ON_SCOPE_EXIT
	{
		for (uint32 SourceId : SourceIds)
		{
			if (TSharedPtr<FSourceRayData> RayData = FindSourceRayData(SourceId))
			{
				FWriteScopeLock Lock(RayData->Lock);
				RayData->bTraceInFlight = false;
			}
		}
		bAudioTraceRunning.store(false);
	};

	if (SourceIds.Num() == 0) return;

	if (!IsWorldTraceable()) return;
	if (!Listener.IsValid()) return;

	FVector ListenerPos;
	{
		FReadScopeLock Lock(Listener->Lock);
		if (!Listener->bPositionSet) return;
		ListenerPos = Listener->Position;
	}

	TArray<FValidData> ValidData;
	if (!GatherDirectData(ListenerPos, SourceIds, ValidData)) return;

	TArray<FLossAccumulator> TotalLoss;
	TotalLoss.Init(FLossAccumulator(), ValidData.Num());

	TArray<FArrivalAccumulator> TotalArrival;
	TotalArrival.Init(FArrivalAccumulator(), ValidData.Num());

	TArray<TArray<FCornerPath>> CornerPaths;
	BounceRaysTrace(ListenerPos, ValidData, TraceSeed, TotalLoss, TotalArrival, CornerPaths);

	TArray<TArray<FArrivalCandidate>> CornerCandidates;
	CornerCandidates.SetNum(ValidData.Num());
	if (GRTAArrivalMode != 0 || GRTADiffraction != 0)
	{
		FindCorners(ListenerPos, CornerPaths, CornerCandidates);
	}

	for (int32 SrcIdx = 0; SrcIdx < ValidData.Num(); ++SrcIdx)
	{
		TraceWriteBack(ListenerPos, ValidData, TotalLoss, TotalArrival, CornerCandidates, SrcIdx);
	}
}

bool FRaytraceManager::GatherDirectData(const FVector& ListenerPos, const TArray<uint32>& SourceIds,
                                        TArray<FValidData>& OutValidData) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RTA_DirectTraces);

	FCollisionQueryParams DirectTraceParams;
	DirectTraceParams.bReturnPhysicalMaterial = true;
	DirectTraceParams.bTraceComplex = true;


	for (uint32 SourceId : SourceIds)
	{
		TSharedPtr<FSourceRayData> RayData = FindSourceRayData(SourceId);
		if (!RayData.IsValid()) continue;

		FValidData Entry;
		Entry.SourceData = RayData;
		Entry.SourceId = SourceId;

		float MinDist, MaxDist, CutoffMin, CutoffMax;
		{
			FReadScopeLock Lock(RayData->Lock);
			Entry.EmitterPos = RayData->EmitterPosition;
			MinDist = RayData->AirAbsorptionMinDistance;
			MaxDist = FMath::Max(RayData->AirAbsorptionMaxDistance, MinDist + KINDA_SMALL_NUMBER);
			CutoffMin = RayData->AirAbsorptionCutoffAtMinDistance;
			CutoffMax = RayData->AirAbsorptionCutoffAtMaxDistance;
		}

		FHitResult DirectHit;
		const bool bBlocked = World->LineTraceSingleByChannel(
			DirectHit, ListenerPos, Entry.EmitterPos, ECC_Visibility, DirectTraceParams);

		Entry.bDirectLOS = !bBlocked;

		const float Distance = FVector::Dist(ListenerPos, Entry.EmitterPos);

		if (bBlocked)
		{
			for (int32 Band = 0; Band < RTA::NumBands; ++Band)
			{
				Entry.DirectTransmissionEnergy[Band] = 1.f;
			}

			FCollisionQueryParams LayerParams = DirectTraceParams;
			FHitResult LayerHit = DirectHit;
			for (int32 Layer = 1; ; ++Layer)
			{
				const UAcousticPhysicalMaterial* PhysMat = Cast<UAcousticPhysicalMaterial>(LayerHit.PhysMaterial.Get());
				const UAcousticMaterialAsset* Mat = PhysMat ? PhysMat->AcousticMaterial.Get() : nullptr;

				if (Mat)
				{
					TArrayView<const float> Transmission = Mat->GetTransmission();
					for (int32 Band = 0; Band < RTA::NumBands; ++Band)
					{
						Entry.DirectTransmissionEnergy[Band] *= FMath::Clamp(Transmission[Band], 0.f, 1.f);
					}
				}
				else
				{
					for (int32 Band = 0; Band < RTA::NumBands; ++Band)
					{
						Entry.DirectTransmissionEnergy[Band] *= RTA::DefaultTransmission;
					}
				}

				UPrimitiveComponent* HitComponent = LayerHit.GetComponent();
				if (!HitComponent) break;
				LayerParams.AddIgnoredComponent(HitComponent);

				FHitResult NextHit;
				if (!World->LineTraceSingleByChannel(NextHit, LayerHit.ImpactPoint, Entry.EmitterPos,
				                                     ECC_Visibility, LayerParams))
				{
					break;
				}

				if (Layer >= MaxTransmissionLayers)
				{
					for (int32 Band = 0; Band < RTA::NumBands; ++Band)
					{
						Entry.DirectTransmissionEnergy[Band] = 0.f; 
					}
					break;
				}
				LayerHit = NextHit;
			}

		}

		const float Alpha = FMath::Clamp((Distance - MinDist) / (MaxDist - MinDist), 0.f, 1.f);
		Entry.AirAbsorptionCutoffHz = FMath::Lerp(CutoffMin, CutoffMax, Alpha);

		OutValidData.Add(MoveTemp(Entry));
	}

	return OutValidData.Num() > 0;
}

float FRaytraceManager::KnifeEdgeLossDb(float Nu)
{
	if (Nu <= -0.78f) return 0.f;
	const float X = Nu - 0.1f;
	return 6.9f + 20.f * FMath::LogX(10.f, FMath::Sqrt(X * X + 1.f) + X);
}

void FRaytraceManager::BounceRaysTrace(const FVector& ListenerPos, const TArray<FValidData>& ValidData,
                                       uint32 TraceSeed, TArray<FLossAccumulator>& TotalLoss, TArray<FArrivalAccumulator>& TotalArrival,
                                       TArray<TArray<FCornerPath>>& OutCornerPaths) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RTA_BounceRays);

	const int32 NumSources = ValidData.Num();
	OutCornerPaths.Reset();
	OutCornerPaths.SetNum(NumSources);
	if (NumSources == 0) return;

	float FurthestSourceCm = 0.f;
	for (const FValidData& Entry : ValidData)
	{
		FurthestSourceCm = FMath::Max(FurthestSourceCm, static_cast<float>(FVector::Dist(ListenerPos, Entry.EmitterPos)));
	}
	const float RayLength = FMath::Clamp(FurthestSourceCm * OcclusionRayLengthHeadroom,
	                                     MinOcclusionRayLengthCm, MaxOcclusionRayLengthCm);
	
	struct FBounce
	{
		FVector Point;
		float PathLengthCm;
		float Energy[RTA::NumBands];
		bool  bPassedSurface;
	};
	using FRayPath = TArray<FBounce, TInlineAllocator<16>>;

	TArray<FRayPath> Paths;
	Paths.SetNum(RayCount);
	
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RTA_BouncePhase);

		ParallelFor(RayCount, [this, &Paths, &ListenerPos, RayLength, TraceSeed](int32 RayNum)
		{
			FCollisionQueryParams BounceParams;
			BounceParams.bReturnPhysicalMaterial = true;
			BounceParams.bTraceComplex = true;

			FRandomStream Rnd;
			Rnd.Initialize(HashCombine(TraceSeed, static_cast<uint32>(RayNum)));

			FVector SegmentStart = ListenerPos;
			FVector PreviousSegmentStart = ListenerPos;
			FVector LastHitNormal = FVector::ZeroVector;
			float LastHitScattering = RTA::DefaultScattering;
			bool bHasHitSurface = false;

			float RayEnergy[RTA::NumBands] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
			float PathLengthCm = 0.f;

			FRayPath& Path = Paths[RayNum];
			
			FVector ForcedDir = FVector::ZeroVector;
			bool bForceDir = false;
			const UPrimitiveComponent* PassedThrough = nullptr;

			const float TransmitFloor = FMath::Pow(10.f, GRTARayTransmitMinDb / 10.f);
			bool bRayPassedSurface = false;

			for (int32 Depth = 0; Depth < OcclusionMaxDepth; ++Depth)
			{
				const FVector RandomDir = bForceDir
					                          ? ForcedDir
					                          : SampleBounceDirection(bHasHitSurface, SegmentStart, PreviousSegmentStart,
					                                                  LastHitNormal, LastHitScattering, Rnd);
				bForceDir = false;

				FHitResult HitResult;
				if (PassedThrough)
				{
					FCollisionQueryParams PastSurfaceParams = BounceParams;
					PastSurfaceParams.AddIgnoredComponent(PassedThrough);
					World->LineTraceSingleByChannel(HitResult, SegmentStart,
					                                SegmentStart + RandomDir * RayLength, ECC_Visibility, PastSurfaceParams);
					PassedThrough = nullptr;
				}
				else
				{
					World->LineTraceSingleByChannel(HitResult, SegmentStart,
					                                SegmentStart + RandomDir * RayLength, ECC_Visibility, BounceParams);
				}
				if (!HitResult.IsValidBlockingHit())
					break;

				PathLengthCm += HitResult.Distance;

				const FSurfaceAcoustics Surface = ResolveSurface(HitResult);
				
				UPrimitiveComponent* HitComponent = HitResult.GetComponent();
				float MeanReflected = 0.f, MeanTransmitted = 0.f;
				for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				{
					MeanReflected   += Surface.Reflected[Band];
					MeanTransmitted += Surface.Transmitted[Band];
				}
				MeanReflected   /= static_cast<float>(RTA::NumBands);
				MeanTransmitted /= static_cast<float>(RTA::NumBands);

				bool bTransmit = false;
				if (GRTARayTransmission != 0 && Surface.bHasMaterial && HitComponent
					&& MeanTransmitted > 0.f && MeanReflected + MeanTransmitted > KINDA_SMALL_NUMBER)
				{
					float TransmitProb = MeanTransmitted / (MeanReflected + MeanTransmitted);
					if (MeanTransmitted >= TransmitFloor)
					{
						TransmitProb = FMath::Max(TransmitProb, GRTARayTransmitMinProb);
					}
					TransmitProb = FMath::Clamp(TransmitProb, 0.f, MaxTransmitProb);

					bTransmit = Rnd.FRand() < TransmitProb;
					for (int32 Band = 0; Band < RTA::NumBands; ++Band)
					{
						RayEnergy[Band] *= bTransmit
							                   ? Surface.Transmitted[Band] / TransmitProb
							                   : Surface.Reflected[Band] / (1.f - TransmitProb);
					}
				}
				else
				{
					for (int32 Band = 0; Band < RTA::NumBands; ++Band)
					{
						RayEnergy[Band] *= Surface.Reflected[Band];
					}
				}

				if (bTransmit)
				{
					PreviousSegmentStart = SegmentStart;
					SegmentStart = FVector(HitResult.ImpactPoint) + RandomDir * SurfaceBiasCm;
					ForcedDir = RandomDir;
					bForceDir = true;
					PassedThrough = HitComponent;
					bRayPassedSurface = true;
					continue;
				}

				PreviousSegmentStart = SegmentStart;
				SegmentStart = HitResult.Location + HitResult.Normal * SurfaceBiasCm;
				LastHitNormal = HitResult.Normal;
				LastHitScattering = Surface.Scattering;
				bHasHitSurface = true;

				FBounce& Bounce = Path.AddDefaulted_GetRef();
				Bounce.Point = SegmentStart;
				Bounce.PathLengthCm = PathLengthCm;
				FMemory::Memcpy(Bounce.Energy, RayEnergy, sizeof(RayEnergy));
				Bounce.bPassedSurface = bRayPassedSurface;
			}
		}, EParallelForFlags::BackgroundPriority);
	}

	static constexpr int32 NumRouteClasses = 2;
	struct FLobeBest
	{
		float Energy = 0.f;
		float Score = -TNumericLimits<float>::Max();
		float LengthCm = 0.f;
		int32 RayNum = INDEX_NONE;
		int32 BounceIdx = INDEX_NONE;
	};
	const int32 NumLobeSlots = NumArrivalLobes * NumRouteClasses;

	struct FNeeContext
	{
		TArray<FLossAccumulator> Loss;
		TArray<FVector> ArrivalSum;
		TArray<float>   ArrivalEnergy;
		TArray<bool>    Connected;
		TArray<FLobeBest> Lobes;
		bool            bInitialised = false;
	};

	TArray<FNeeContext> Contexts;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RTA_BounceNeeRays);

		const FRTABatchedTracer Tracer(World.Get(), /*bTraceComplex*/ false);

		if (!Tracer.IsValid())
		{
			UE_LOG(LogRTA, Warning, TEXT("BounceRaysTrace: batched tracer unavailable, using engine traces"));
		}

		auto IsBlocked = [this, &Tracer](const FVector& A, const FVector& B)
		{
			return Tracer.IsValid()
				       ? Tracer.TraceTest(A, B)
				       : World->LineTraceTestByChannel(A, B, ECC_Visibility);
		};
		
		const bool  bSpreading = GRTAReflectionSpreading != 0;
		const float PrecedenceMetres = PrecedenceWindowMetres();

		TArray<float> StraightCm;
		StraightCm.SetNumUninitialized(NumSources);
		for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
		{
			StraightCm[SrcIdx] = FVector::Dist(ListenerPos, ValidData[SrcIdx].EmitterPos);
		}

		ParallelForWithTaskContext(Contexts, RayCount,
		                           [&Paths, &ValidData, &IsBlocked, &ListenerPos, &StraightCm, NumSources, NumLobeSlots, bSpreading, PrecedenceMetres]
	                           (FNeeContext& Ctx, int32 RayNum)
		                           {
			                           if (!Ctx.bInitialised)
			                           {
				                           Ctx.Loss.Init(FLossAccumulator(), NumSources);
				                           Ctx.ArrivalSum.Init(FVector::ZeroVector, NumSources);
				                           Ctx.ArrivalEnergy.Init(0.f, NumSources);
				                           Ctx.Connected.SetNumUninitialized(NumSources);
				                           Ctx.Lobes.Init(FLobeBest(), NumSources * NumLobeSlots);
				                           Ctx.bInitialised = true;
			                           }

			                           for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
			                           {
				                           Ctx.Connected[SrcIdx] = ValidData[SrcIdx].bDirectLOS;
			                           }

			                           const FRayPath& Path = Paths[RayNum];
			                           const FVector FirstDir = Path.Num() > 0
				                                                    ? (Path[0].Point - ListenerPos).GetSafeNormal()
				                                                    : FVector::ZeroVector;
			                           const int32 FirstLobe = Path.Num() > 0 ? ArrivalLobeIndex(FirstDir) : INDEX_NONE;

			                           for (int32 BounceIdx = 0; BounceIdx < Path.Num(); ++BounceIdx)
			                           {
				                           const FBounce& Bounce = Path[BounceIdx];
				                           for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
				                           {
					                           if (Ctx.Connected[SrcIdx])
						                           continue;

					                           if (IsBlocked(Bounce.Point, ValidData[SrcIdx].EmitterPos))
						                           continue;

					                           const float ShadowDistanceCm = FVector::Dist(Bounce.Point, ValidData[SrcIdx].EmitterPos);
					                           const float TotalPathCm = Bounce.PathLengthCm + ShadowDistanceCm;
					                           const float TotalPathMetres = TotalPathCm * RTA::CmToMetres;
						
					                           const float Spreading = bSpreading
						                                                   ? FMath::Square(StraightCm[SrcIdx] / FMath::Max(TotalPathCm, StraightCm[SrcIdx]))
						                                                   : 1.f;

					                           float MeanArriving = 0.f;
					                           for (int32 Band = 0; Band < RTA::NumBands; ++Band)
					                           {
						                           const float AirAtten = FMath::Exp(-RTA::AirAbsorptionPerMetre[Band] * TotalPathMetres);
						                           const float ArrivingEnergy = FMath::Clamp(Bounce.Energy[Band] * AirAtten * Spreading, 0.f, 1.f);
						                           Ctx.Loss[SrcIdx].Bands[Band] += 1.f - ArrivingEnergy;
						                           MeanArriving += ArrivingEnergy;
					                           }
					                           MeanArriving /= float(RTA::NumBands);
						
					                           const float Precedence = PrecedenceWeight(
						                           (TotalPathCm - StraightCm[SrcIdx]) * RTA::CmToMetres, PrecedenceMetres);
					                           Ctx.ArrivalSum[SrcIdx]    += FirstDir * MeanArriving * Precedence;
					                           Ctx.ArrivalEnergy[SrcIdx] += MeanArriving * Precedence;
				                           	
					                           if (FirstLobe != INDEX_NONE)
					                           {
						                           const int32 RouteClass = Bounce.bPassedSurface ? 1 : 0;
						                           FLobeBest& Lobe = Ctx.Lobes[SrcIdx * NumLobeSlots + FirstLobe * NumRouteClasses + RouteClass];
						                           Lobe.Energy += MeanArriving;
						                           const float Score = RouteClass == 0 ? -TotalPathCm : MeanArriving;
						                           if (Score > Lobe.Score)
						                           {
							                           Lobe.Score     = Score;
							                           Lobe.LengthCm  = TotalPathCm;
							                           Lobe.RayNum    = RayNum;
							                           Lobe.BounceIdx = BounceIdx;
						                           }
					                           }

					                           Ctx.Connected[SrcIdx] = true;
				                           }
			                           }

			                           for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
			                           {
				                           if (!Ctx.Connected[SrcIdx])
				                           {
					                           for (int32 Band = 0; Band < RTA::NumBands; ++Band)
					                           {
						                           Ctx.Loss[SrcIdx].Bands[Band] += 1.f;
					                           }
				                           }
			                           }
		                           },
		                           EParallelForFlags::BackgroundPriority);
	}   // lock released

	for (const FNeeContext& Ctx : Contexts)
	{
		if (!Ctx.bInitialised) continue;

		for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
		{
			for (int32 Band = 0; Band < RTA::NumBands; ++Band)
			{
				TotalLoss[SrcIdx].Bands[Band] += Ctx.Loss[SrcIdx].Bands[Band];
			}

			TotalArrival[SrcIdx].Sum    += Ctx.ArrivalSum[SrcIdx];
			TotalArrival[SrcIdx].Energy += Ctx.ArrivalEnergy[SrcIdx];
		}
	}
	
	if (GRTAArrivalMode != 0 || GRTADiffraction != 0)
	{
		TArray<FLobeBest> Merged;
		Merged.Init(FLobeBest(), NumSources * NumLobeSlots);
		for (const FNeeContext& Ctx : Contexts)
		{
			if (!Ctx.bInitialised) continue;
			for (int32 i = 0; i < Merged.Num(); ++i)
			{
				const FLobeBest& In = Ctx.Lobes[i];
				FLobeBest& Out = Merged[i];
				Out.Energy += In.Energy;
				if (In.RayNum != INDEX_NONE && In.Score > Out.Score)
				{
					Out.Score     = In.Score;
					Out.LengthCm  = In.LengthCm;
					Out.RayNum    = In.RayNum;
					Out.BounceIdx = In.BounceIdx;
				}
			}
		}

		const int32 MaxCandidates = FMath::Clamp(GRTACornerCandidates, 0, NumLobeSlots);
		for (int32 SrcIdx = 0; SrcIdx < NumSources; ++SrcIdx)
		{
			if (ValidData[SrcIdx].bDirectLOS || MaxCandidates == 0) continue;
			
			TArray<int32, TInlineAllocator<NumArrivalLobes * 2>> SlotOrder;
			for (int32 Slot = 0; Slot < NumLobeSlots; ++Slot)
			{
				const FLobeBest& L = Merged[SrcIdx * NumLobeSlots + Slot];
				if (L.RayNum != INDEX_NONE && L.Energy > 0.f) SlotOrder.Add(Slot);
			}
			SlotOrder.Sort([&Merged, SrcIdx, NumLobeSlots](int32 A, int32 B)
			{
				return Merged[SrcIdx * NumLobeSlots + A].Energy > Merged[SrcIdx * NumLobeSlots + B].Energy;
			});

			for (int32 i = 0; i < FMath::Min(MaxCandidates, SlotOrder.Num()); ++i)
			{
				const int32 Slot = SlotOrder[i];
				const FLobeBest& L = Merged[SrcIdx * NumLobeSlots + Slot];
				const FRayPath& Path = Paths[L.RayNum];

				FCornerPath& Corner = OutCornerPaths[SrcIdx].AddDefaulted_GetRef();
				Corner.Verts.Add(ListenerPos);
				Corner.CumLengthCm.Add(0.f);
				for (int32 b = 0; b <= L.BounceIdx; ++b)
				{
					Corner.Verts.Add(Path[b].Point);
					Corner.CumLengthCm.Add(Path[b].PathLengthCm);
				}
				Corner.Verts.Add(ValidData[SrcIdx].EmitterPos);
				Corner.CumLengthCm.Add(L.LengthCm);
				Corner.LobeEnergy = L.Energy;
				Corner.bPassedSurface = (Slot % NumRouteClasses) == 1;
			}
		}
	}

	if (GRTAValidateBatchedTrace != 0)
	{
		TArray<TPair<FVector, FVector>> Pairs;
		for (int32 RayNum = 0; RayNum < RayCount && Pairs.Num() < 2000; RayNum += 7)
		{
			for (const FBounce& Bounce : Paths[RayNum])
			{
				for (int32 SrcIdx = 0; SrcIdx < NumSources && Pairs.Num() < 2000; SrcIdx += 5)
				{
					Pairs.Emplace(Bounce.Point, ValidData[SrcIdx].EmitterPos);
				}
			}
		}

		TArray<bool> Fast;
		Fast.Reserve(Pairs.Num());
		{
			const FRTABatchedTracer Tracer(World.Get(), false);
			for (const auto& P : Pairs) Fast.Add(Tracer.TraceTest(P.Key, P.Value));
		}

		int32 Mismatches = 0;
		for (int32 i = 0; i < Pairs.Num(); ++i)
		{
			const bool bSlow = World->LineTraceTestByChannel(Pairs[i].Key, Pairs[i].Value, ECC_Visibility);
			Mismatches += (Fast[i] != bSlow);
		}

		UE_LOG(LogRTA, Log, TEXT("BatchedTrace validation: %d / %d mismatches"), Mismatches, Pairs.Num());
	}
}

int32 FRaytraceManager::ArrivalLobeIndex(const FVector& Dir)
{
	static constexpr float Split = 0.3827f;
	auto Q = [](double C) { return C > Split ? 2 : (C < -Split ? 0 : 1); };
	return Q(Dir.X) * 9 + Q(Dir.Y) * 3 + Q(Dir.Z);
}

void FRaytraceManager::FindCorners(const FVector& ListenerPos, const TArray<TArray<FCornerPath>>& CornerPaths,
                                   TArray<TArray<FArrivalCandidate>>& OutCandidates) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RTA_FindCorners);

	struct FJob { int32 SrcIdx; int32 PathIdx; };
	TArray<FJob> Jobs;
	for (int32 SrcIdx = 0; SrcIdx < CornerPaths.Num(); ++SrcIdx)
	{
		for (int32 PathIdx = 0; PathIdx < CornerPaths[SrcIdx].Num(); ++PathIdx)
		{
			Jobs.Add({ SrcIdx, PathIdx });
		}
	}
	if (Jobs.Num() == 0) return;

	TArray<FArrivalCandidate> JobResults;
	JobResults.SetNum(Jobs.Num());
	TArray<uint8> JobValid;
	JobValid.Init(0, Jobs.Num());
	TArray<FVector> JobBlocked;
	JobBlocked.SetNum(Jobs.Num());
	TArray<uint8> JobBisected;
	JobBisected.Init(0, Jobs.Num());
	TArray<int32> JobK;
	JobK.Init(INDEX_NONE, Jobs.Num());

	{
		const FRTABatchedTracer Tracer(World.Get(), /*bTraceComplex*/ false);

		auto IsBlocked = [this, &Tracer](const FVector& A, const FVector& B)
		{
			return Tracer.IsValid()
				       ? Tracer.TraceTest(A, B)
				       : World->LineTraceTestByChannel(A, B, ECC_Visibility);
		};

		ParallelFor(Jobs.Num(), [&](int32 JobIdx)
		{
			const FCornerPath& Path = CornerPaths[Jobs[JobIdx].SrcIdx][Jobs[JobIdx].PathIdx];
			const int32 Last = Path.Verts.Num() - 1;
			if (Last < 2) return;
			
			int32 K = 1;
			for (int32 i = 2; i <= Last; ++i)
			{
				if (!IsBlocked(ListenerPos, Path.Verts[i])) K = i;
			}

			FArrivalCandidate& Out = JobResults[JobIdx];
			Out.Kind = EArrivalKind::Corner;
			Out.Weight = Path.LobeEnergy;

			if (K == Last)
			{
				Out.Point = Path.Verts[Last];
				Out.LengthCm = FVector::Dist(ListenerPos, Out.Point);
			}
			else
			{
				FVector Lo = Path.Verts[K];
				FVector Hi = Path.Verts[K + 1];
				for (int32 Step = 0; Step < CornerBisectSteps; ++Step)
				{
					const FVector Mid = (Lo + Hi) * 0.5;
					if (!IsBlocked(ListenerPos, Mid)) Lo = Mid;
					else                              Hi = Mid;
				}

				const FVector& Emitter = Path.Verts[Last];
				const double DistListener = FVector::Dist2D(ListenerPos, Lo);
				const double DistEmitter  = FVector::Dist2D(Lo, Emitter);
				if (DistListener + DistEmitter > 1.0)
				{
					FVector Levelled = Lo;
					Levelled.Z = FMath::Lerp(ListenerPos.Z, Emitter.Z, DistListener / (DistListener + DistEmitter));
					if (!IsBlocked(ListenerPos, Levelled)) Lo = Levelled;
				}
				Out.Point = Lo;
				JobBlocked[JobIdx] = Hi;
				JobBisected[JobIdx] = 1;
				JobK[JobIdx] = K;
				
				Out.LengthCm = FVector::Dist(ListenerPos, Lo)
				             + FVector::Dist(Lo, Path.Verts[K + 1])
				             + (Path.CumLengthCm[Last] - Path.CumLengthCm[K + 1]);
			}

			const FVector ToPoint = Out.Point - ListenerPos;
			if (ToPoint.SizeSquared() < 1.f) return;
			Out.Direction = ToPoint.GetSafeNormal();
			JobValid[JobIdx] = 1;
		}, EParallelForFlags::BackgroundPriority);
	}   // lock released
	
	FCollisionQueryParams AnchorParams;
	AnchorParams.bTraceComplex = true;
	constexpr double MinAnchorDistanceCm = 100.0;

	for (int32 JobIdx = 0; JobIdx < Jobs.Num(); ++JobIdx)
	{
		if (!JobValid[JobIdx]) continue;

		FArrivalCandidate& Out = JobResults[JobIdx];
		Out.Anchor = Out.Point;

		if (JobBisected[JobIdx])
		{
			FHitResult EdgeHit;
			if (World->LineTraceSingleByChannel(EdgeHit, ListenerPos, JobBlocked[JobIdx], ECC_Visibility, AnchorParams))
			{
				const FCornerPath& Path = CornerPaths[Jobs[JobIdx].SrcIdx][Jobs[JobIdx].PathIdx];
				const int32 K = JobK[JobIdx];
				UPrimitiveComponent* EdgeComponent = EdgeHit.GetComponent();

				bool bThroughSurface = false;
				if (GRTARayTransmission != 0 && Path.bPassedSurface && EdgeComponent && K != INDEX_NONE)
				{
					FHitResult SegmentHit;
					bThroughSurface = World->LineTraceSingleByChannel(SegmentHit, Path.Verts[K], Path.Verts[K + 1],
					                                                  ECC_Visibility, AnchorParams)
						&& SegmentHit.GetComponent() == EdgeComponent;
				}

				if (bThroughSurface)
				{
					FVector Anchor = FVector(EdgeHit.ImpactPoint) - Out.Direction * SurfaceAnchorPullbackCm;
					const FBoxSphereBounds& Bounds = EdgeComponent->Bounds;
					if (Bounds.BoxExtent.GetMax() * 2.0 <= GRTADoorMaxSizeCm)
					{
						FCollisionQueryParams CentreParams = AnchorParams;
						CentreParams.AddIgnoredComponent(EdgeComponent);
						if (!World->LineTraceTestByChannel(ListenerPos, Bounds.Origin, ECC_Visibility, CentreParams))
						{
							Anchor = Bounds.Origin;
						}
					}

					const FVector ToAnchor = Anchor - ListenerPos;
					if (ToAnchor.SizeSquared() > 1.0)
					{
						Out.Kind = EArrivalKind::Surface;
						Out.Anchor = Anchor;
						Out.Direction = ToAnchor.GetUnsafeNormal();
					}
				}
				else
				{
					const double MaxT = FVector::Dist(ListenerPos, Out.Point);
					const double T = FVector::DotProduct(FVector(EdgeHit.ImpactPoint) - ListenerPos, Out.Direction);
					Out.Anchor = ListenerPos + Out.Direction * FMath::Clamp(T, FMath::Min(MinAnchorDistanceCm, MaxT), MaxT);
				}
			}
		}

		OutCandidates[Jobs[JobIdx].SrcIdx].Add(Out);
	}
}

void FRaytraceManager::TraceWriteBack(const FVector& ListenerPos, const TArray<FValidData>& ValidData,
                                      const TArray<FLossAccumulator>& TotalLoss, const TArray<FArrivalAccumulator>& TotalArrival,
                                      const TArray<TArray<FArrivalCandidate>>& CornerCandidates, const int32 SrcIdx)
{
	const FValidData& Data = ValidData[SrcIdx];
	const TSharedPtr<FSourceRayData>& RayData = Data.SourceData;

	FSourceResult Published;
	const FVector ToSource = Data.EmitterPos - ListenerPos;
	const float SourceDistanceCm = ToSource.Size();
	const FVector DirToSource = SourceDistanceCm > KINDA_SMALL_NUMBER
		                            ? ToSource / SourceDistanceCm
		                            : FVector::ForwardVector;
	
	auto CornerDiffractionBands = [&ListenerPos, &Data, SourceDistanceCm](const FVector& Corner, float* OutEnergy)
	{
		const float PathCm = FVector::Dist(ListenerPos, Corner) + FVector::Dist(Corner, Data.EmitterPos);
		const float ExcessCm = FMath::Max(PathCm - SourceDistanceCm, 0.f);
		const float ExcessMetres = ExcessCm * RTA::CmToMetres;
		const float Spreading = PathCm > KINDA_SMALL_NUMBER ? FMath::Square(SourceDistanceCm / PathCm) : 1.f;
		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			const float Lambda = SpeedOfSoundMps / RTA::FrequencyBands[Band];
			const float Nu = 2.f * FMath::Sqrt(ExcessMetres / Lambda);
			OutEnergy[Band] = FMath::Clamp(FMath::Pow(10.f, -KnifeEdgeLossDb(Nu) / 10.f) * Spreading, 0.f, 1.f);
		}
		return ExcessCm;
	};
	auto MeanOfBands = [](const float* Energy)
	{
		float Mean = 0.f;
		for (int32 Band = 0; Band < RTA::NumBands; ++Band) Mean += Energy[Band];
		return Mean / static_cast<float>(RTA::NumBands);
	};
	auto IsRealCorner = [&Data](const FArrivalCandidate& C)
	{
		return C.Kind == EArrivalKind::Corner && FVector::DistSquared(C.Point, Data.EmitterPos) > 1.0;
	};

	float DiffractionEnergy[RTA::NumBands] = {};
	const FArrivalCandidate* DiffractionCorner = nullptr;
	float DiffractionExcessCm = 0.f;
	if (!Data.bDirectLOS && GRTADiffraction != 0)
	{
		float BestExcessCm = TNumericLimits<float>::Max();
		for (const FArrivalCandidate& C : CornerCandidates[SrcIdx])
		{
			if (!IsRealCorner(C)) continue;
			const float ExcessCm = FVector::Dist(ListenerPos, C.Anchor) + FVector::Dist(C.Anchor, Data.EmitterPos) - SourceDistanceCm;
			if (ExcessCm < BestExcessCm)
			{
				BestExcessCm = ExcessCm;
				DiffractionCorner = &C;
			}
		}
		if (DiffractionCorner)
		{
			DiffractionExcessCm = CornerDiffractionBands(DiffractionCorner->Anchor, DiffractionEnergy);
		}
	}

	float MeanTransmission = 0.f;
	for (int32 Band = 0; Band < RTA::NumBands; ++Band)
	{
		MeanTransmission += Data.DirectTransmissionEnergy[Band];
	}
	MeanTransmission = FMath::Clamp(MeanTransmission / static_cast<float>(RTA::NumBands), 0.f, 1.f);
	const float MeanDiffraction = FMath::Clamp(MeanOfBands(DiffractionEnergy), 0.f, 1.f);
	const float MeanDirect = FMath::Min(MeanTransmission + MeanDiffraction, 1.f);

	const FVector AroundDir = DiffractionCorner
		                          ? (DiffractionCorner->Anchor - ListenerPos).GetSafeNormal()
		                          : DirToSource;

	const FArrivalAccumulator& Arrival = TotalArrival[SrcIdx];
	
	const float IndirectFraction  = Arrival.Energy / static_cast<float>(RayCount);
	const float DirectWeight      = MeanTransmission;
	const float DiffractionWeight = DiffractionCorner
		                                ? MeanDiffraction * PrecedenceWeight(DiffractionExcessCm * RTA::CmToMetres, PrecedenceWindowMetres())
		                                : 0.f;
	const float IndirectWeight    = (1.f - MeanDirect) * IndirectFraction;
	const float TotalWeight       = DirectWeight + DiffractionWeight + IndirectWeight;
	
	const FVector IndirectMean = Arrival.Energy > KINDA_SMALL_NUMBER
		                             ? Arrival.Sum / Arrival.Energy
		                             : FVector::ZeroVector;
	
	const FVector RawArrival = TotalWeight > KINDA_SMALL_NUMBER
		                           ? (DirToSource * DirectWeight + AroundDir * DiffractionWeight + IndirectMean * IndirectWeight) / TotalWeight
		                           : DirToSource;

	const float CornerAngleDeg = DiffractionCorner
		                             ? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(RawArrival.GetSafeNormal(), AroundDir), -1.f, 1.f)))
		                             : -1.f;

	UE_LOG(LogRTA, Verbose, TEXT("SourceId=%u W[through]=%.3f W[around]=%.3f W[reflect]=%.3f AngleToCorner=%.0fdeg"),
	       Data.SourceId, DirectWeight, DiffractionWeight, IndirectWeight, CornerAngleDeg);
	
	FArrivalCandidate Winner;
	Winner.Kind = EArrivalKind::Blend;
	Winner.Direction = RawArrival.GetSafeNormal(UE_SMALL_NUMBER, DirToSource);

	if (GRTAArrivalMode != 0)
	{
		FVector LastWinnerDir = FVector::ZeroVector;
		bool bHasLastWinner = false;
		{
			FReadScopeLock Lock(RayData->Lock);
			LastWinnerDir = RayData->LastWinnerDir;
			bHasLastWinner = RayData->bHasLastWinner;
		}

		if (Data.bDirectLOS)
		{
			Winner.Kind = EArrivalKind::Direct;
			Winner.Direction = DirToSource;
			Winner.Point = Data.EmitterPos;
			Winner.Anchor = Data.EmitterPos;
			Winner.LengthCm = SourceDistanceCm;
			Winner.Weight = 1.f;
		}
		else
		{
			TArray<FArrivalCandidate, TInlineAllocator<8>> Candidates;

			if (MeanTransmission > 0.f)
			{
				FArrivalCandidate& C = Candidates.AddDefaulted_GetRef();
				C.Kind = EArrivalKind::Through;
				C.Direction = DirToSource;
				C.Point = Data.EmitterPos;
				C.Anchor = Data.EmitterPos;
				C.LengthCm = SourceDistanceCm;
				C.Weight = MeanTransmission;
			}

			const float MergeCos = FMath::Cos(FMath::DegreesToRadians(CandidateMergeDeg));
			
			const float IndirectScale = (1.f - MeanDirect) / static_cast<float>(RayCount);
			for (const FArrivalCandidate& Corner : CornerCandidates[SrcIdx])
			{
				FArrivalCandidate C = Corner;
				C.Weight *= IndirectScale;

				if (GRTADiffraction != 0 && IsRealCorner(C))
				{
					float Bands[RTA::NumBands];
					CornerDiffractionBands(C.Anchor, Bands);
					C.Weight += MeanOfBands(Bands);
				}

				if (C.Weight > 0.f) Candidates.Add(C);
			}
			
			for (int32 i = 0; i < Candidates.Num(); ++i)
			{
				for (int32 j = Candidates.Num() - 1; j > i; --j)
				{
					FArrivalCandidate& A = Candidates[i];
					const FArrivalCandidate& B = Candidates[j];
					if (FVector::DotProduct(A.Direction, B.Direction) < MergeCos) continue;

					const float MergedWeight = A.Weight + B.Weight;
					if (B.LengthCm < A.LengthCm) A = B;
					A.Weight = MergedWeight;
					Candidates.RemoveAt(j);
				}
			}

			Candidates.Sort([](const FArrivalCandidate& A, const FArrivalCandidate& B)
			{
				return A.LengthCm < B.LengthCm;
			});

			const float HystCos = FMath::Cos(FMath::DegreesToRadians(HysteresisMatchDeg));
			auto ScoreDb = [&](const FArrivalCandidate& C)
			{
				float Db = 10.f * FMath::LogX(10.f, FMath::Max(C.Weight, 1.0e-12f));
				if (bHasLastWinner && FVector::DotProduct(C.Direction, LastWinnerDir) > HystCos)
				{
					Db += GRTAPrecedenceHystDb;
				}
				return Db;
			};

			if (Candidates.Num() > 0)
			{
				int32 Pick = 0;
				for (int32 i = 1; i < Candidates.Num(); ++i)
				{
					if (ScoreDb(Candidates[i]) > ScoreDb(Candidates[Pick]) + GRTAPrecedenceDb) Pick = i;
				}
				Winner = Candidates[Pick];
			}
			else
			{
				Winner.Kind = EArrivalKind::Hold;
				Winner.Direction = bHasLastWinner ? LastWinnerDir : DirToSource;
			}

			if (UE_LOG_ACTIVE(LogRTA, Verbose))
			{
				FString List;
				for (const FArrivalCandidate& C : Candidates)
				{
					List += FString::Printf(TEXT(" %s(%.0fcm %.1fdB)"), ArrivalKindName(static_cast<uint8>(C.Kind)),
					                        C.LengthCm, 10.f * FMath::LogX(10.f, FMath::Max(C.Weight, 1.0e-12f)));
				}
				UE_LOG(LogRTA, Verbose, TEXT("SourceId=%u Routes:%s -> %s"), Data.SourceId, *List,
				       ArrivalKindName(static_cast<uint8>(Winner.Kind)));
			}
		}
	}

	{
		FWriteScopeLock Lock(RayData->Lock);

		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			const float IndirectLoss = TotalLoss[SrcIdx].Bands[Band] / float(RayCount);
			const float IndirectEnergy = 1.f - IndirectLoss;
			const float DirectEnergy = FMath::Min(
				Data.DirectTransmissionEnergy[Band] + DiffractionEnergy[Band], 1.f);

			const float FinalEnergy = DirectEnergy + (1.f - DirectEnergy) * IndirectEnergy;
			const float NewEstimate = FMath::Clamp(1.f - FinalEnergy, 0.f, 1.f);

			RayData->DirectTransmissionLoss[Band] = RayData->bHasValidEstimate
				                                        ? FMath::Lerp(RayData->DirectTransmissionLoss[Band], NewEstimate, OcclusionSmoothingAlpha)
				                                        : NewEstimate;
		}

		RayData->DirectLowpassCutoffHz = RayData->bHasValidEstimate
			                                 ? FMath::Lerp(RayData->DirectLowpassCutoffHz, ValidData[SrcIdx].AirAbsorptionCutoffHz, OcclusionSmoothingAlpha)
			                                 : ValidData[SrcIdx].AirAbsorptionCutoffHz;
		
		const FVector TargetArrival = GRTAArrivalMode != 0 ? Winner.Direction : RawArrival;
		
		float DirAlpha = OcclusionSmoothingAlpha;
		if (GRTAArrivalMode != 0)
		{
			const bool bExactRoute = Winner.Kind == EArrivalKind::Direct || Winner.Kind == EArrivalKind::Through;
			const bool bRouteSwitch = RayData->bHasLastWinner && Winner.Kind != EArrivalKind::Hold
				&& FVector::DotProduct(Winner.Direction, RayData->LastWinnerDir)
				   < FMath::Cos(FMath::DegreesToRadians(HysteresisMatchDeg));
			if (bExactRoute || bRouteSwitch)
			{
				DirAlpha = FMath::Clamp(GRTAArrivalFastAlpha, OcclusionSmoothingAlpha, 1.f);
			}
		}

		RayData->SmoothedArrival = RayData->bHasValidEstimate
			                           ? FMath::Lerp(RayData->SmoothedArrival, TargetArrival, DirAlpha)
			                           : TargetArrival;

		RayData->SmoothedBlend = RayData->bHasValidEstimate
			                         ? FMath::Lerp(RayData->SmoothedBlend, RawArrival, OcclusionSmoothingAlpha)
			                         : RawArrival;
		
		const bool bAnchoredRoute = GRTAArrivalMode != 0
			&& Winner.Kind != EArrivalKind::Hold && Winner.Kind != EArrivalKind::Blend;
		if (bAnchoredRoute)
		{
			const float AnchorAlpha = Winner.Kind == EArrivalKind::Direct || !RayData->bHasSmoothedAnchor ? 1.f : DirAlpha;
			RayData->SmoothedAnchor = FMath::Lerp(RayData->SmoothedAnchor, Winner.Anchor, AnchorAlpha);
			RayData->bHasSmoothedAnchor = true;
		}
		else if (GRTAArrivalMode == 0)
		{
			RayData->bHasSmoothedAnchor = false;
		}

		if (GRTAArrivalMode != 0 && Winner.Kind != EArrivalKind::Hold)
		{
			RayData->LastWinnerDir = Winner.Direction;
			RayData->bHasLastWinner = true;
		}

		RayData->bHasValidEstimate = true;

		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			Published.DirectTransmissionLoss[Band] = RayData->DirectTransmissionLoss[Band];
		}
		Published.DirectLowpassCutoffHz = RayData->DirectLowpassCutoffHz;

		const float DirLength = RayData->SmoothedArrival.Size();
		Published.ArrivalDirection = DirLength > KINDA_SMALL_NUMBER
			                             ? RayData->SmoothedArrival / DirLength
			                             : DirToSource;
		Published.ArrivalFocus = FMath::Clamp(static_cast<float>(RayData->SmoothedBlend.Size()), 0.f, 1.f);

		Published.VirtualPosition = ListenerPos + Published.ArrivalDirection * SourceDistanceCm;
		
		if (GRTAArrivalMode != 0)
		{
			Published.bHasCorner = Winner.Kind == EArrivalKind::Corner || Winner.Kind == EArrivalKind::Surface;
			Published.CornerPoint = Winner.Anchor;
		}
		else
		{
			Published.bHasCorner = DiffractionCorner != nullptr;
			Published.CornerPoint = DiffractionCorner ? DiffractionCorner->Anchor : FVector::ZeroVector;
		}
		Published.ArrivalKind = static_cast<uint8>(Winner.Kind);
		Published.bHasAnchor = GRTAArrivalMode != 0 && RayData->bHasSmoothedAnchor;
		Published.ArrivalAnchor = RayData->SmoothedAnchor;

		Published.bHasValidEstimate = true;
	}

	RayData->Result.Write(Published);

	const float OffAxisDeg = FMath::RadiansToDegrees(FMath::Acos(
		FMath::Clamp(FVector::DotProduct(Published.ArrivalDirection, DirToSource), -1.f, 1.f)));

	UE_LOG(LogRTA, Verbose, TEXT("SourceId=%u Loss=[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f] CutoffHz=%.0f Arrival=(%.2f,%.2f,%.2f) OffAxis=%.0fdeg Focus=%.2f Corner=%d Excess=%.0fcm Diff=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]"),
	       Data.SourceId,
	       Published.DirectTransmissionLoss[0], Published.DirectTransmissionLoss[1],
	       Published.DirectTransmissionLoss[2], Published.DirectTransmissionLoss[3],
	       Published.DirectTransmissionLoss[4], Published.DirectTransmissionLoss[5],
	       Published.DirectLowpassCutoffHz,
	       Published.ArrivalDirection.X, Published.ArrivalDirection.Y, Published.ArrivalDirection.Z,
	       OffAxisDeg, Published.ArrivalFocus,
	       DiffractionCorner ? 1 : 0, DiffractionExcessCm,
	       DiffractionEnergy[0], DiffractionEnergy[1],
	       DiffractionEnergy[2], DiffractionEnergy[3],
	       DiffractionEnergy[4], DiffractionEnergy[5]);
}

bool FRaytraceManager::TickRoomProbe(float /*DeltaTime*/)
{
	bool bExpected = false;
	if (!bRoomProbeRunning.compare_exchange_strong(bExpected, true))
	{
		return true;
	}

	if (!Listener.IsValid())
	{
		bRoomProbeRunning.store(false);
		return true;
	}

	{
		FReadScopeLock Lock(Listener->Lock);
		if (!Listener->bPositionSet)
		{
			bRoomProbeRunning.store(false);
			return true;
		}
	}

	const uint32 TraceSeed = static_cast<uint32>(TraceCounter.Add(1));

	TWeakPtr<FRaytraceManager, ESPMode::ThreadSafe> WeakSelf = AsWeak();
	UE::Tasks::Launch(TEXT("RTA_RoomProbe"), [WeakSelf, TraceSeed]()
	{
		if (TSharedPtr<FRaytraceManager, ESPMode::ThreadSafe> Self = WeakSelf.Pin())
		{
			Self->RunRoomProbe(TraceSeed);
		}
	}, LowLevelTasks::ETaskPriority::BackgroundLow);

	return true;
}

void FRaytraceManager::RunRoomProbe(uint32 TraceSeed)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RTA_RoomProbe);

	ON_SCOPE_EXIT { bRoomProbeRunning.store(false); };

	if (!IsWorldTraceable() || !Listener.IsValid()) return;

	FVector ListenerPos;
	{
		FReadScopeLock Lock(Listener->Lock);
		if (!Listener->bPositionSet) return;
		ListenerPos = Listener->Position;
	}

	const EParallelForFlags PFFlags = (GRTAProbeParallel != 0)
		                                  ? EParallelForFlags::BackgroundPriority
		                                  : EParallelForFlags::ForceSingleThread;

	struct FProbeBounce
	{
		FVector Point;
		FVector Normal;
		float PathLengthCm;
		float Scattering;
		float Outgoing[RTA::NumBands];
	};

	TArray<TArray<FProbeBounce>> Paths;
	Paths.SetNum(RayCount);

	struct FBounceStats
	{
		double PathLengthSumCm = 0.0;
		double AbsorptionSum[RTA::NumBands] = {};
		int32  HitCount = 0;
		int32  EscapedRays = 0;
		float  MaxHitDistanceCm = 0.f;
	};

	TArray<FBounceStats> BounceStats;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RTA_ProbeBounce);

		const float RouletteEnergy = FMath::Max(GRTAProbeRouletteEnergy, KINDA_SMALL_NUMBER);

		ParallelForWithTaskContext(BounceStats, RayCount,
		                           [this, &Paths, &ListenerPos, TraceSeed, RouletteEnergy](FBounceStats& S, int32 RayNum)
		                           {
			                           FCollisionQueryParams Params;
			                           Params.bReturnPhysicalMaterial = true;
			                           Params.bTraceComplex = true;

			                           FRandomStream Rnd;
			                           Rnd.Initialize(HashCombine(TraceSeed, static_cast<uint32>(RayNum)));

			                           FVector SegmentStart = ListenerPos;
			                           FVector PreviousSegmentStart = ListenerPos;
			                           FVector LastHitNormal = FVector::ZeroVector;
			                           float LastHitScattering = RTA::DefaultScattering;
			                           bool bHasHitSurface = false;

			                           float RayEnergy[RTA::NumBands] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
			                           float PathLengthCm = 0.f;

			                           TArray<FProbeBounce>& Path = Paths[RayNum];
			                           Path.Reserve(32);

			                           for (int32 Depth = 0; Depth < ProbeMaxDepth; ++Depth)
			                           {
				                           const float RemainingPathCm = FEchogram::MaxPathCm - PathLengthCm;
				                           if (RemainingPathCm <= 0.f)
					                           break;

				                           const FVector RandomDir = SampleBounceDirection(
					                           bHasHitSurface, SegmentStart, PreviousSegmentStart,
					                           LastHitNormal, LastHitScattering, Rnd);

				                           FHitResult HitResult;
				                           World->LineTraceSingleByChannel(HitResult, SegmentStart,
				                                                           SegmentStart + RandomDir * RemainingPathCm, ECC_Visibility, Params);

				                           if (!HitResult.IsValidBlockingHit())
				                           {
					                           ++S.EscapedRays;
					                           for (int32 Band = 0; Band < RTA::NumBands; ++Band)
					                           {
						                           S.AbsorptionSum[Band] += 1.0;
					                           }
					                           ++S.HitCount;
					                           break;
				                           }

				                           PathLengthCm += HitResult.Distance;
				                           S.PathLengthSumCm += HitResult.Distance;
				                           S.MaxHitDistanceCm = FMath::Max(S.MaxHitDistanceCm, HitResult.Distance);
				                           ++S.HitCount;

				                           const FSurfaceAcoustics Surface = ResolveSurface(HitResult);
				                           for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				                           {
					                           S.AbsorptionSum[Band] += Surface.Absorption[Band];
				                           }

				                           FProbeBounce& Bounce = Path.AddDefaulted_GetRef();
				                           Bounce.Point = HitResult.Location;
				                           Bounce.Normal = HitResult.Normal;
				                           Bounce.PathLengthCm = PathLengthCm;
				                           Bounce.Scattering = Surface.Scattering;
				                           for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				                           {
					                           Bounce.Outgoing[Band] = RayEnergy[Band] * Surface.Reflected[Band];
				                           }

				                           for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				                           {
					                           RayEnergy[Band] *= Surface.Reflected[Band];
				                           }

				                           PreviousSegmentStart = SegmentStart;
				                           SegmentStart = HitResult.Location + HitResult.Normal * SurfaceBiasCm;
				                           LastHitNormal = HitResult.Normal;
				                           LastHitScattering = Surface.Scattering;
				                           bHasHitSurface = true;

				                           float PeakEnergy = 0.f;
				                           for (const float Energy : RayEnergy)
				                           {
					                           PeakEnergy = FMath::Max(PeakEnergy, Energy);
				                           }

				                           if (PeakEnergy < EnergyFloor)
					                           break;

				                           if (PeakEnergy < RouletteEnergy)
				                           {
					                           const float q = FMath::Clamp(PeakEnergy / RouletteEnergy, RouletteQMin, RouletteQMax);
					                           if (Rnd.FRand() > q)
						                           break;

					                           for (float& Energy : RayEnergy)
					                           {
						                           Energy /= q;
					                           }
				                           }
			                           }
		                           },
		                           PFFlags);
	}

	FBounceStats Stats;
	for (const FBounceStats& S : BounceStats)
	{
		Stats.PathLengthSumCm += S.PathLengthSumCm;
		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			Stats.AbsorptionSum[Band] += S.AbsorptionSum[Band];
		}
		Stats.HitCount += S.HitCount;
		Stats.EscapedRays += S.EscapedRays;
		Stats.MaxHitDistanceCm = FMath::Max(Stats.MaxHitDistanceCm, S.MaxHitDistanceCm);
	}

	struct FShadowContext
	{
		FEchogram Echo;
		int32 Considered = 0;
		int32 OutOfWindow = 0;
		int32 Rouletted = 0;
		int32 Traced = 0;
		bool bInitialised = false;
	};

	TArray<FShadowContext> ShadowContexts;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RTA_ProbeShadow);

		const FRTABatchedTracer Tracer(World.Get(), /*bTraceComplex*/ true);

		auto IsBlocked = [this, &Tracer](const FVector& A, const FVector& B)
		{
			if (Tracer.IsValid())
			{
				return Tracer.TraceTest(A, B);
			}
			FCollisionQueryParams P;
			P.bTraceComplex = true;
			return World->LineTraceTestByChannel(A, B, ECC_Visibility, P);
		};

		const float Threshold = GRTAProbeShadowThreshold;
		constexpr float BiasCm = SurfaceBiasCm;

		ParallelForWithTaskContext(ShadowContexts, RayCount,
		                           [&Paths, &ListenerPos, &IsBlocked, TraceSeed, Threshold, BiasCm](FShadowContext& C, int32 RayNum)
		                           {
			                           if (!C.bInitialised)
			                           {
				                           C.Echo.Reset();
				                           C.bInitialised = true;
			                           }

			                           FRandomStream ShadowRnd;
			                           ShadowRnd.Initialize(HashCombine(HashCombine(TraceSeed, static_cast<uint32>(RayNum)), 0x5AD0u));

			                           for (const FProbeBounce& Bounce : Paths[RayNum])
			                           {
				                           const FVector ToListener = ListenerPos - Bounce.Point;
				                           const float TrueDistanceCm = ToListener.Size();
				                           if (TrueDistanceCm <= KINDA_SMALL_NUMBER)
					                           continue;

				                           const float CosTheta = FVector::DotProduct(Bounce.Normal, ToListener / TrueDistanceCm);
				                           if (CosTheta <= 0.f)
					                           continue;

				                           ++C.Considered;

				                           const float ShadowDistanceCm = FMath::Max(TrueDistanceCm, RTA::MinReceiverDistanceCm);
				                           const float TotalPathCm = Bounce.PathLengthCm + ShadowDistanceCm;

				                           const int32 Bin = C.Echo.BinFromPathLengthCm(TotalPathCm);
				                           if (Bin <= 0)
				                           {
					                           ++C.OutOfWindow;
					                           continue;
				                           }

				                           const float TotalPathMetres = TotalPathCm * RTA::CmToMetres;
				                           const float SolidAngleFraction =
					                           FMath::Square(RTA::ReceiverRadiusCm) / FMath::Square(ShadowDistanceCm);
				                           const float GeometryTerm = Bounce.Scattering * CosTheta * SolidAngleFraction;

				                           float Contribution[RTA::NumBands];
				                           float PeakContribution = 0.f;
				                           for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				                           {
					                           const float AirAtten = FMath::Exp(-RTA::AirAbsorptionPerMetre[Band] * TotalPathMetres);
					                           Contribution[Band] = Bounce.Outgoing[Band] * GeometryTerm * AirAtten;
					                           PeakContribution = FMath::Max(PeakContribution, Contribution[Band]);
				                           }

				                           const float SurvivalP = (Threshold > 0.f)
					                                                   ? FMath::Clamp(PeakContribution / Threshold, ShadowRouletteMinP, 1.f)
					                                                   : 1.f;

				                           if (SurvivalP < 1.f && ShadowRnd.FRand() >= SurvivalP)
				                           {
					                           ++C.Rouletted;
					                           continue;
				                           }

				                           ++C.Traced;

				                           if (IsBlocked(Bounce.Point + Bounce.Normal * BiasCm, ListenerPos))
					                           continue;

				                           const float Weight = 1.f / SurvivalP;
				                           for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				                           {
					                           C.Echo.At(Band, Bin) += Contribution[Band] * Weight;
				                           }
			                           }
		                           },
		                           PFFlags);
	}   // lock released here

	FEchogram& Fresh = Listener->FreshEchogram;
	Fresh.Reset();

	int32 ShadowsConsidered = 0, ShadowsOutOfWindow = 0, ShadowsRouletted = 0, ShadowsTraced = 0;
	for (const FShadowContext& C : ShadowContexts)
	{
		if (!C.bInitialised) continue;

		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			for (int32 Bin = 0; Bin < FEchogram::NumBins; ++Bin)
			{
				Fresh.At(Band, Bin) += C.Echo.At(Band, Bin);
			}
		}

		ShadowsConsidered += C.Considered;
		ShadowsOutOfWindow += C.OutOfWindow;
		ShadowsRouletted += C.Rouletted;
		ShadowsTraced += C.Traced;
	}

	if (GRTAValidateProbeTrace != 0)
	{
		TArray<TPair<FVector, FVector>> Pairs;
		for (int32 RayNum = 0; RayNum < RayCount && Pairs.Num() < 2000; RayNum += 3)
		{
			for (const FProbeBounce& Bounce : Paths[RayNum])
			{
				if (Pairs.Num() >= 2000) break;
				Pairs.Emplace(Bounce.Point + Bounce.Normal * SurfaceBiasCm, ListenerPos);
			}
		}

		TArray<bool> Fast;
		Fast.Reserve(Pairs.Num());
		{
			const FRTABatchedTracer Tracer(World.Get(), true);
			for (const auto& P : Pairs) Fast.Add(Tracer.TraceTest(P.Key, P.Value));
		}

		FCollisionQueryParams P;
		P.bTraceComplex = true;
		int32 Mismatches = 0;
		for (int32 i = 0; i < Pairs.Num(); ++i)
		{
			Mismatches += Fast[i] != World->LineTraceTestByChannel(Pairs[i].Key, Pairs[i].Value, ECC_Visibility, P);
		}
		UE_LOG(LogRTA, Log, TEXT("Probe trace validation: %d / %d mismatches"), Mismatches, Pairs.Num());
	}

	constexpr float NormFactor = 1.f / (static_cast<float>(RayCount) * PI * FMath::Square(RTA::ReceiverRadiusCm));
	Fresh.Scale(NormFactor);

	FRoomResult Published;

	TStaticArray<FDecayMetric, RTA::NumBands> DecayMetrics;
	int32 ProbeCountSnapshot = 0;

	{
		FWriteScopeLock Lock(Listener->Lock);

		++Listener->ProbeCount;
		ProbeCountSnapshot = Listener->ProbeCount;
		Listener->AccumulatedEchogram.Accumulate(Fresh, Listener->AccumulatedEchogram.GetSmoothingAlpha());

		for (int Band = 0; Band < RTA::NumBands; ++Band)
		{
			DecayMetrics[Band] = RTA::ComputeDecayMetrics(Listener->AccumulatedEchogram, Band);
		}

		if (Stats.HitCount > 0)
		{
			Listener->MeanFreePathMetres = static_cast<float>(Stats.PathLengthSumCm / Stats.HitCount) * RTA::CmToMetres;
			for (int32 Band = 0; Band < RTA::NumBands; ++Band)
			{
				Listener->MeanAbsorption[Band] = FMath::Clamp(
					static_cast<float>(Stats.AbsorptionSum[Band] / Stats.HitCount), 0.f, 0.99f);
			}
			ComputeEyringRT60(Listener->MeanFreePathMetres, Listener->MeanAbsorption, Listener->EyringRT60);
			Listener->bHasValidEstimate = true;
		}

		Listener->EscapedRayFraction = static_cast<float>(Stats.EscapedRays) / static_cast<float>(RayCount);
		Listener->MeanBounceDepth = static_cast<float>(Stats.HitCount) / static_cast<float>(RayCount);

		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			Published.EyringRT60[Band] = Listener->EyringRT60[Band];
			Published.MeanAbsorption[Band] = Listener->MeanAbsorption[Band];
			Published.MeasuredT30[Band] = DecayMetrics[Band].T30;
			Published.MeasuredT20[Band] = DecayMetrics[Band].T20;
			Published.bT30Valid[Band]   = DecayMetrics[Band].bT30Valid;
			Published.bT20Valid[Band]   = DecayMetrics[Band].bT20Valid;
		}
		{
			const int32 FirstBin = Listener->AccumulatedEchogram.FirstNonZeroBin(3);
			Published.FirstReflectionSeconds = FirstBin > 0	? static_cast<float>(FirstBin) * FEchogram::BinWidthSeconds	: 0.f;
		}

		Published.MeanFreePathMetres = Listener->MeanFreePathMetres;
		Published.EscapedRayFraction = Listener->EscapedRayFraction;
		Published.MeanBounceDepth = Listener->MeanBounceDepth;
		Published.bHasValidEstimate = Listener->bHasValidEstimate;
	}

	Listener->Result.Write(Published);

	{
		FReadScopeLock Lock(Listener->Lock);
		UE_LOG(LogRTA, Verbose,
		       TEXT("RoomProbe: MFP=%.2fm Hits=%d MeanDepth=%.1f, MeanAbsorption=[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f], Escaped=%.0f%% MaxHit=%.0fcm "
			       "Eyring=[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f] BandTotal[1k]=%.3e LastBin[1k]=%d"),
		       Published.MeanFreePathMetres, Stats.HitCount, Published.MeanBounceDepth,
		       Published.MeanAbsorption[0], Published.MeanAbsorption[1], Published.MeanAbsorption[2],
		       Published.MeanAbsorption[3], Published.MeanAbsorption[4], Published.MeanAbsorption[5],
		       Published.EscapedRayFraction * 100.f,
		       Stats.MaxHitDistanceCm,
		       Published.EyringRT60[0], Published.EyringRT60[1], Published.EyringRT60[2],
		       Published.EyringRT60[3], Published.EyringRT60[4], Published.EyringRT60[5],
		       Listener->AccumulatedEchogram.BandTotal(3),
		       Listener->AccumulatedEchogram.LastNonZeroBin(3));
	}

	UE_LOG(LogRTA, Verbose, TEXT("RoomProbe shadows: %d considered, %d out of window, %d rouletted, %d traced (%.0f%% skipped)"),
	       ShadowsConsidered, ShadowsOutOfWindow, ShadowsRouletted, ShadowsTraced,
	       ShadowsConsidered > 0 ? 100.f * (1.f - float(ShadowsTraced) / float(ShadowsConsidered)) : 0.f);

	if (GRTAShoebox != 0)
	{
		LogShoeboxReport(Published, DecayMetrics, ProbeCountSnapshot);
	}
}

void FRaytraceManager::LogShoeboxReport(const FRoomResult& Published,
                                        const TStaticArray<FDecayMetric, RTA::NumBands>& DecayMetrics, int32 ProbeCount)
{
	const double X = GRTAShoeboxX;
	const double Y = GRTAShoeboxY;
	const double Z = GRTAShoeboxZ;
	const double Alpha = GRTAShoeboxAlpha;

	if (X <= 0.0 || Y <= 0.0 || Z <= 0.0 || Alpha <= 0.0 || Alpha >= 1.0)
	{
		UE_LOG(LogRTA, Warning, TEXT("Shoebox: invalid dimensions or alpha; set rta.Shoebox.X/Y/Z and rta.Shoebox.Alpha"));
		return;
	}

	const double V = X * Y * Z;
	const double S = 2.0 * (X * Y + Y * Z + Z * X);
	const double MfpRef = 4.0 * V / S;

	auto Pct = [](double Measured, double Ref)
	{
		return Ref > 0.0 ? 100.0 * (Measured / Ref - 1.0) : 0.0;
	};
	auto Verdict = [](bool bPass) { return bPass ? TEXT("PASS") : TEXT("FAIL"); };

	const double EscapedPct = double(Published.EscapedRayFraction) * 100.0;
	const bool bSealed = Published.EscapedRayFraction <= 0.f;

	float MaxAlphaErr = 0.f;
	for (int32 Band = 0; Band < RTA::NumBands; ++Band)
	{
		MaxAlphaErr = FMath::Max(MaxAlphaErr, FMath::Abs(Published.MeanAbsorption[Band] - float(Alpha)));
	}
	const bool bMaterial = MaxAlphaErr < ShoeboxAlphaTolerance;

	const double MfpErr = Pct(Published.MeanFreePathMetres, MfpRef);
	const bool bMfp = FMath::Abs(MfpErr) <= ShoeboxMfpTolerancePct;

	UE_LOG(LogRTA, Log, TEXT("=== Shoebox %.2f x %.2f x %.2f m  alpha %.3f  probe %d ==="), X, Y, Z, Alpha, ProbeCount);
	UE_LOG(LogRTA, Log, TEXT("  1 Sealed     escaped %.2f%%                                  %s"),
	       EscapedPct, Verdict(bSealed));
	UE_LOG(LogRTA, Log, TEXT("  2 Material   max |alpha - %.3f| = %.4f                     %s"),
	       Alpha, MaxAlphaErr, Verdict(bMaterial));
	UE_LOG(LogRTA, Log, TEXT("  3 MFP        %.4f m  vs 4V/S %.4f m  (%+.2f%%)             %s"),
	       Published.MeanFreePathMetres, MfpRef, MfpErr, Verdict(bMfp));

	UE_LOG(LogRTA, Log, TEXT("  4/5    band   analytic    traced (err)        T30 (err)         |R|   curv   win"));

	for (int32 Band = 0; Band < RTA::NumBands; ++Band)
	{
		const double M = RTA::AirAbsorptionPerMetre[Band];
		const double Analytic = 0.161 * V / (-S * FMath::Loge(1.0 - Alpha) + 4.0 * M * V);

		const FDecayMetric& D = DecayMetrics[Band];
		const double TracedErr = Pct(Published.EyringRT60[Band], Analytic);
		const double T30Err = D.bT30Valid ? Pct(D.T30, Analytic) : 0.0;
		const bool bT30 = D.bT30Valid && FMath::Abs(T30Err) <= ShoeboxT30TolerancePct;

		const FString T30Text = D.bT30Valid
			                        ? FString::Printf(TEXT("%7.3f s (%+5.1f%%)"), D.T30, T30Err)
			                        : FString(TEXT("    invalid       "));

		UE_LOG(LogRTA, Log, TEXT("       %5.0f Hz  %7.3f s  %7.3f s (%+5.1f%%)  %s  %5.3f  %+5.1f%%  %d   %s"),
		       RTA::FrequencyBands[Band], Analytic,
		       Published.EyringRT60[Band], TracedErr,
		       *T30Text,
		       FMath::Abs(D.T30_R), D.CurvaturePercent, D.bWindowSufficient ? 1 : 0,
		       Verdict(bT30));
	}

	if (!bSealed || !bMaterial)
	{
		UE_LOG(LogRTA, Log, TEXT("  Checks 1-2 failed: fix the room or the material before reading 3-5."));
	}
	else if (ProbeCount < 10)
	{
		UE_LOG(LogRTA, Log, TEXT("  Still converging (%d probes); T30 will drift until about 10."), ProbeCount);
	}
}

void FRaytraceManager::ComputeEyringRT60(float MeanFreePathMetres,
                                         const float MeanAbsorption[RTA::NumBands], float OutRT60[RTA::NumBands])
{
	constexpr float EyringConstant = 0.161f;

	if (MeanFreePathMetres <= KINDA_SMALL_NUMBER)
	{
		for (int32 Band = 0; Band < RTA::NumBands; ++Band) OutRT60[Band] = 0.f;
		return;
	}

	const float Numerator = EyringConstant * (MeanFreePathMetres * 0.25f);

	for (int32 Band = 0; Band < RTA::NumBands; ++Band)
	{
		const float Alpha = FMath::Clamp(MeanAbsorption[Band], 0.f, 0.99f);
		const float SurfaceTerm = -FMath::Loge(1.f - Alpha);
		const float AirTerm = RTA::AirAbsorptionPerMetre[Band] * MeanFreePathMetres;
		const float Denominator = SurfaceTerm + AirTerm;

		OutRT60[Band] = Denominator > KINDA_SMALL_NUMBER
			                ? FMath::Min(Numerator / Denominator, 20.f) : 20.f;
	}
}

TSharedPtr<FRaytraceManager::FSourceRayData> FRaytraceManager::FindSourceRayData(uint32 SourceId) const
{
	FReadScopeLock Lock(ResultsLock);
	const TSharedPtr<FSourceRayData>* Entry = Results.Find(SourceId);
	return Entry ? *Entry : nullptr;
}


void FRaytraceManager::ApplyEmitterPositionLocked(FSourceRayData& RayData, const FVector& Position)
{
	const bool bFirstSet = !RayData.bEmitterPositionSet;
	if (bFirstSet ||
		FVector::DistSquared(RayData.EmitterPosition, Position) > FMath::Square(MinPositionDeltaForDirty))
	{
		RayData.EmitterPosition = Position;
		RayData.bEmitterPositionSet = true;
		RayData.bDirty = true;
	}
}

bool FRaytraceManager::IsWorldTraceable() const
{
	return World.IsValid()
		&& World->GetPhysicsScene() != nullptr
		&& World->GetPhysicsScene()->GetSolver() != nullptr;
}

FVector FRaytraceManager::SampleBounceDirection(bool bHasHitSurface, const FVector& SegmentStart,
                                                const FVector& PreviousSegmentStart, const FVector& LastHitNormal, float LastHitScattering,
                                                FRandomStream& RndStrm)
{
	if (!bHasHitSurface)
	{
		return RndStrm.VRand();
	}

	if (RndStrm.FRand() < LastHitScattering)
	{
		return RandomCosineWeightedHemisphere(LastHitNormal, RndStrm);
	}

	const FVector IncomingDir = (SegmentStart - PreviousSegmentStart).GetSafeNormal();
	return FMath::GetReflectionVector(IncomingDir, LastHitNormal);
}

FVector FRaytraceManager::RandomCosineWeightedHemisphere(const FVector& Normal, FRandomStream& RndStrm)
{
	const float U1 = RndStrm.FRand();
	const float U2 = RndStrm.FRand();
	const float r = FMath::Sqrt(U1);
	const float Theta = 2.f * PI * U2;

	const float x = r * FMath::Cos(Theta);
	const float y = r * FMath::Sin(Theta);
	const float z = FMath::Sqrt(FMath::Max(0.f, 1.f - U1));

	const FVector Up = FMath::Abs(Normal.Z) < 0.999f ? FVector(0, 0, 1) : FVector(1, 0, 0);
	const FVector Tangent = FVector::CrossProduct(Up, Normal).GetSafeNormal();
	const FVector Bitangent = FVector::CrossProduct(Normal, Tangent);

	return (Tangent * x + Bitangent * y + Normal * z).GetSafeNormal();
}


FRaytraceManager::FSurfaceAcoustics FRaytraceManager::ResolveSurface(const FHitResult& HitResult)
{
	FSurfaceAcoustics Out;

	const UAcousticPhysicalMaterial* PhysMat = Cast<UAcousticPhysicalMaterial>(HitResult.PhysMaterial.Get());
	const UAcousticMaterialAsset* Mat = PhysMat ? PhysMat->AcousticMaterial.Get() : nullptr;

	if (Mat)
	{
		TArrayView<const float> Absorption = Mat->GetAbsorption();
		TArrayView<const float> Transmission = Mat->GetTransmission();

		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			Out.Absorption[Band] = FMath::Clamp(Absorption[Band], 0.f, 1.f);
			Out.Transmitted[Band] = FMath::Clamp(Transmission[Band], 0.f, 1.f);
			Out.Reflected[Band] = FMath::Clamp(1.f - Absorption[Band] - Transmission[Band], 0.f, 1.f);
		}
		Out.Scattering = Mat->GetBakedScattering();
		Out.bHasMaterial = true;
	}
	else
	{
		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			Out.Absorption[Band] = RTA::DefaultAbsorption;
			Out.Reflected[Band] = RTA::DefaultReflection;
		}
		Out.Scattering = RTA::DefaultScattering;
	}

	return Out;
}