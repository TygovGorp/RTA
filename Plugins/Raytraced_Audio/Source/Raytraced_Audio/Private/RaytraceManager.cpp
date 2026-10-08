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
		TEXT("Draw each source's true direction (red), apparent direction (green) and diffraction detour (yellow)."),
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
		TEXT("1 = knife-edge diffraction around the obstacle blocking the direct path. 0 = off."),
		ECVF_Default);

	constexpr float SpeedOfSoundMps     = 343.f;
	constexpr int32 DetourNumDirections = 8;
	constexpr int32 DetourBisectSteps   = 3;
	constexpr int32 DetourSlideSteps    = 5;  
	constexpr float DetourRadiiCm[]     = { 10.f, 20.f, 40.f, 80.f, 160.f, 320.f, 640.f, 1280.f };

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

	/** Window length in metres for the current rta.PrecedenceMs, or 0 when the weighting is off. */
	float PrecedenceWindowMetres()
	{
		return GRTAPrecedenceMs > 0.f ? SpeedOfSoundMps * GRTAPrecedenceMs * 0.001f : 0.f;
	}

	/** Direction weight for a route arriving ExtraPathMetres later than the straight line would. */
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

void FRaytraceManager::UpdateEmitterPosition(uint32 SourceId, const FVector& Position)
{
	TSharedPtr<FSourceRayData> RayData = FindSourceRayData(SourceId);
	if (!RayData.IsValid()) return;

	FWriteScopeLock Lock(RayData->Lock);
	const bool bFirstSet = !RayData->bEmitterPositionSet;
	if (bFirstSet ||
		FVector::DistSquared(RayData->EmitterPosition, Position) > FMath::Square(MinPositionDeltaForDirty))
	{
		RayData->EmitterPosition = Position;
		RayData->bEmitterPositionSet = true;
		RayData->bDirty = true;
	}
}

void FRaytraceManager::UpdateListenerPosition(const FVector& Position)
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
		RayData->Result.TryRead(Out); 
	}
	return Out;
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

TSharedPtr<FRaytraceManager::FSourceRayData> FRaytraceManager::FindSourceRayData(uint32 SourceId) const
{
	FReadScopeLock Lock(ResultsLock);
	const TSharedPtr<FSourceRayData>* Entry = Results.Find(SourceId);
	return Entry ? *Entry : nullptr;
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
	const float u1 = RndStrm.FRand();
	const float u2 = RndStrm.FRand();
	const float r = FMath::Sqrt(u1);
	const float Theta = 2.f * PI * u2;

	const float x = r * FMath::Cos(Theta);
	const float y = r * FMath::Sin(Theta);
	const float z = FMath::Sqrt(FMath::Max(0.f, 1.f - u1));

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
			Out.Reflected[Band] = FMath::Clamp(1.f - Absorption[Band] - Transmission[Band], 0.f, 1.f);
		}
		Out.Scattering = Mat->GetBakedScattering();
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

		// Where it is heard from
		DrawDebugLine(DrawWorld, Origin, Apparent, FColor::Green, false, LifeTime, 0, 3.f);

		// Sphere sized by spread
		const float Radius = FMath::Lerp(10.f, 80.f, 1.f - Result.ArrivalFocus);
		DrawDebugSphere(DrawWorld, Apparent, Radius, 12, FColor::Green, false, LifeTime, 0, 1.f);

		DrawDebugString(DrawWorld, Apparent + FVector(0.f, 0.f, Radius + 10.f),
			FString::Printf(TEXT("focus %.2f"), Result.ArrivalFocus),
			nullptr, FColor::White, LifeTime);

		// Shortest path around the blocking obstacle
		if (Result.bHasDetour)
		{
			DrawDebugLine(DrawWorld, ListenerPos, Result.DetourPoint, FColor::Yellow, false, LifeTime, 0, 1.f);
			DrawDebugLine(DrawWorld, Result.DetourPoint, EmitterPos, FColor::Yellow, false, LifeTime, 0, 1.f);
			DrawDebugPoint(DrawWorld, Result.DetourPoint, 8.f, FColor::Yellow, false, LifeTime);
			DrawDebugString(DrawWorld, Result.DetourPoint + FVector(0.f, 0.f, 15.f),
				FString::Printf(TEXT("excess %.0f cm"), Result.DetourExcessCm),
				nullptr, FColor::Yellow, LifeTime);
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

	const float LifeTime = AudioTraceTickInterval * 1.5f;

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

			Items.Add({ Entry.Key, EmitterPos, Result });
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
				Item.Result.bHasDetour ? TEXT("  detour") : TEXT("")),
			nullptr, FColor::Cyan, LifeTime);
	}
#endif
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

	BounceRaysTrace(ListenerPos, ValidData, TraceSeed, TotalLoss, TotalArrival);

	for (int32 SrcIdx = 0; SrcIdx < ValidData.Num(); ++SrcIdx)
	{
		TraceWriteBack(ListenerPos, ValidData, TotalLoss, TotalArrival, SrcIdx);
	}
}

float FRaytraceManager::KnifeEdgeLossDb(float Nu)
{
	// ITU-R P.526 single knife-edge approximation, valid for Nu > about -0.78
	if (Nu <= -0.78f) return 0.f;
	const float X = Nu - 0.1f;
	return 6.9f + 20.f * FMath::LogX(10.f, FMath::Sqrt(X * X + 1.f) + X);
}

FRaytraceManager::FDetour FRaytraceManager::FindDetour(const FVector& ListenerPos, const FVector& EmitterPos,
	const FVector& BlockPoint, const FCollisionQueryParams& Params) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RTA_FindDetour);

	FDetour Best;

	const FVector ToSource = EmitterPos - ListenerPos;
	const float DirectCm = ToSource.Size();
	if (DirectCm <= KINDA_SMALL_NUMBER) return Best;

	const FVector Axis = ToSource / DirectCm;
	const FVector Helper = FMath::Abs(Axis.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
	const FVector U = FVector::CrossProduct(Axis, Helper).GetSafeNormal();
	const FVector V = FVector::CrossProduct(Axis, U);

	auto IsClear = [this, &ListenerPos, &EmitterPos, &Params](const FVector& P)
	{
		return !World->LineTraceTestByChannel(ListenerPos, P, ECC_Visibility, Params)
			&& !World->LineTraceTestByChannel(EmitterPos, P, ECC_Visibility, Params);
	};

	auto ExcessOf = [&ListenerPos, &EmitterPos, DirectCm](const FVector& P)
	{
		return FMath::Max(FVector::Dist(ListenerPos, P) + FVector::Dist(P, EmitterPos) - DirectCm, 0.f);
	};

	auto DirectionFor = [&U, &V](int32 DirIdx)
	{
		const float Angle = 2.f * PI * float(DirIdx) / float(DetourNumDirections);
		return U * FMath::Cos(Angle) + V * FMath::Sin(Angle);
	};

	auto RadialSearch = [&IsClear](const FVector& Center, const FVector& Dir, FVector& OutP)
	{
		float PrevRadius = 0.f;
		for (const float Radius : DetourRadiiCm)
		{
			if (!IsClear(Center + Dir * Radius))
			{
				PrevRadius = Radius;
				continue;
			}

			float Lo = PrevRadius;
			float Hi = Radius;
			for (int32 Step = 0; Step < DetourBisectSteps; ++Step)
			{
				const float Mid = 0.5f * (Lo + Hi);
				if (IsClear(Center + Dir * Mid)) Hi = Mid;
				else                             Lo = Mid;
			}
			OutP = Center + Dir * Hi;
			return true;
		}
		return false;
	};

	auto Consider = [&Best, &ExcessOf](const FVector& P)
	{
		const float Excess = ExcessOf(P);
		if (Excess < Best.ExcessCm)
		{
			Best.Point = P;
			Best.ExcessCm = Excess;
			Best.bFound = true;
		}
		return Excess;
	};

	const float TFront = FVector::DotProduct(BlockPoint - ListenerPos, Axis) / DirectCm;
	float TBack = TFront;
	{
		FHitResult BackHit;
		if (World->LineTraceSingleByChannel(BackHit, EmitterPos, ListenerPos, ECC_Visibility, Params))
		{
			TBack = FVector::DotProduct(BackHit.ImpactPoint - ListenerPos, Axis) / DirectCm;
		}
	}
	const float TLo = FMath::Clamp(FMath::Min(TFront, TBack), 0.f, 1.f);
	const float THi = FMath::Clamp(FMath::Max(TFront, TBack), 0.f, 1.f);

	auto CenterAt = [&ListenerPos, &Axis, DirectCm](float T)
	{
		return ListenerPos + Axis * (T * DirectCm);
	};
	
	int32 BestDir = INDEX_NONE;
	const float Phase1T[] = { TFront, 0.5f * (TLo + THi), TBack };
	for (const float T : Phase1T)
	{
		const FVector Center = CenterAt(T);
		for (int32 DirIdx = 0; DirIdx < DetourNumDirections; ++DirIdx)
		{
			FVector P;
			if (RadialSearch(Center, DirectionFor(DirIdx), P))
			{
				const float Before = Best.ExcessCm;
				if (Consider(P) < Before) BestDir = DirIdx;
			}
		}
		if (Best.bFound) break;
	}

	if (!Best.bFound) return Best;
	
	if ((THi - TLo) * DirectCm > 1.f)
	{
		const FVector Dir = DirectionFor(BestDir);
		auto Evaluate = [&](float T)
		{
			FVector P;
			return RadialSearch(CenterAt(T), Dir, P) ? Consider(P) : TNumericLimits<float>::Max();
		};

		constexpr float InvPhi = 0.618034f;
		float A = TLo, B = THi;
		float C = B - InvPhi * (B - A);
		float D = A + InvPhi * (B - A);
		float FC = Evaluate(C);
		float FD = Evaluate(D);

		for (int32 Iter = 0; Iter < DetourSlideSteps; ++Iter)
		{
			if (FC < FD)
			{
				B = D;  D = C;  FD = FC;
				C = B - InvPhi * (B - A);
				FC = Evaluate(C);
			}
			else
			{
				A = C;  C = D;  FC = FD;
				D = A + InvPhi * (B - A);
				FD = Evaluate(D);
			}
		}
	}

	return Best;
}

bool FRaytraceManager::GatherDirectData(const FVector& ListenerPos, const TArray<uint32>& SourceIds,
	TArray<FValidData>& OutValidData) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RTA_DirectTraces);

	FCollisionQueryParams DirectTraceParams;
	DirectTraceParams.bReturnPhysicalMaterial = true;
	DirectTraceParams.bTraceComplex = true;

	FCollisionQueryParams DetourTraceParams;
	DetourTraceParams.bTraceComplex = true;   // same geometry as the direct trace

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
			const FSurfaceAcoustics Surface = ResolveSurface(DirectHit);
			const UAcousticPhysicalMaterial* PhysMat =
				Cast<UAcousticPhysicalMaterial>(DirectHit.PhysMaterial.Get());
			const UAcousticMaterialAsset* Mat = PhysMat ? PhysMat->AcousticMaterial.Get() : nullptr;

			if (Mat)
			{
				TArrayView<const float> Transmission = Mat->GetTransmission();
				for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				{
					Entry.DirectTransmissionEnergy[Band] = FMath::Clamp(Transmission[Band], 0.f, 1.f);
				}
			}
			else
			{
				for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				{
					Entry.DirectTransmissionEnergy[Band] = RTA::DefaultTransmission;
				}
			}

			if (GRTADiffraction != 0)
			{
				const FDetour Detour = FindDetour(ListenerPos, Entry.EmitterPos,
				                                  DirectHit.ImpactPoint, DetourTraceParams);
				if (Detour.bFound)
				{
					const float ExcessMetres = Detour.ExcessCm * RTA::CmToMetres;
					const float DetourCm = Distance + Detour.ExcessCm;
					
					const float Spreading = DetourCm > KINDA_SMALL_NUMBER
						? FMath::Square(Distance / DetourCm)
						: 1.f;

					for (int32 Band = 0; Band < RTA::NumBands; ++Band)
					{
						const float Lambda = SpeedOfSoundMps / RTA::FrequencyBands[Band];
						const float Nu = 2.f * FMath::Sqrt(ExcessMetres / Lambda);
						const float LossDb = KnifeEdgeLossDb(Nu);
						Entry.DirectDiffractionEnergy[Band] =
							FMath::Clamp(FMath::Pow(10.f, -LossDb / 10.f) * Spreading, 0.f, 1.f);
					}

					Entry.bHasDetour = true;
					Entry.DetourPoint = Detour.Point;
					Entry.DetourExcessCm = Detour.ExcessCm;
				}
			}
		}

		const float Alpha = FMath::Clamp((Distance - MinDist) / (MaxDist - MinDist), 0.f, 1.f);
		Entry.AirAbsorptionCutoffHz = FMath::Lerp(CutoffMin, CutoffMax, Alpha);

		OutValidData.Add(MoveTemp(Entry));
	}

	return OutValidData.Num() > 0;
}

void FRaytraceManager::BounceRaysTrace(const FVector& ListenerPos, const TArray<FValidData>& ValidData,
	uint32 TraceSeed, TArray<FLossAccumulator>& TotalLoss, TArray<FArrivalAccumulator>& TotalArrival) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RTA_BounceRays);

	const int32 NumSources = ValidData.Num();
	if (NumSources == 0) return;

	float FurthestSourceCm = 0.f;
	for (const FValidData& Entry : ValidData)
	{
		FurthestSourceCm = FMath::Max(FurthestSourceCm, float(FVector::Dist(ListenerPos, Entry.EmitterPos)));
	}
	const float RayLength = FMath::Clamp(FurthestSourceCm * OcclusionRayLengthHeadroom,
	                                     MinOcclusionRayLengthCm, MaxOcclusionRayLengthCm);
	
	struct FBounce
	{
		FVector Point;
		float PathLengthCm;
		float Energy[RTA::NumBands];
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

			for (int32 Depth = 0; Depth < OcclusionMaxDepth; ++Depth)
			{
				const FVector RandomDir = SampleBounceDirection(
					bHasHitSurface, SegmentStart, PreviousSegmentStart,
					LastHitNormal, LastHitScattering, Rnd);

				FHitResult HitResult;
				World->LineTraceSingleByChannel(HitResult, SegmentStart,
					SegmentStart + RandomDir * RayLength, ECC_Visibility, BounceParams);
				if (!HitResult.IsValidBlockingHit())
					break;

				PathLengthCm += HitResult.Distance;

				const FSurfaceAcoustics Surface = ResolveSurface(HitResult);
				for (int32 Band = 0; Band < RTA::NumBands; ++Band)
				{
					RayEnergy[Band] *= Surface.Reflected[Band];
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
			}
		}, EParallelForFlags::BackgroundPriority);
	}

	struct FNeeContext
	{
		TArray<FLossAccumulator> Loss;
		TArray<FVector> ArrivalSum;
		TArray<float>   ArrivalEnergy;
		TArray<bool>    Connected;
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
			[&Paths, &ValidData, &IsBlocked, &ListenerPos, &StraightCm, NumSources, bSpreading, PrecedenceMetres]
			(FNeeContext& Ctx, int32 RayNum)
			{
				if (!Ctx.bInitialised)
				{
					Ctx.Loss.Init(FLossAccumulator(), NumSources);
					Ctx.ArrivalSum.Init(FVector::ZeroVector, NumSources);
					Ctx.ArrivalEnergy.Init(0.f, NumSources);
					Ctx.Connected.SetNumUninitialized(NumSources);
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

				for (const FBounce& Bounce : Path)
				{
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
							Ctx.Loss[SrcIdx].Bands[Band] += (1.f - ArrivingEnergy);
							MeanArriving += ArrivingEnergy;
						}
						MeanArriving /= float(RTA::NumBands);
						
						const float Precedence = PrecedenceWeight(
							(TotalPathCm - StraightCm[SrcIdx]) * RTA::CmToMetres, PrecedenceMetres);
						Ctx.ArrivalSum[SrcIdx]    += FirstDir * MeanArriving * Precedence;
						Ctx.ArrivalEnergy[SrcIdx] += MeanArriving * Precedence;

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

void FRaytraceManager::TraceWriteBack(const FVector& ListenerPos, const TArray<FValidData>& ValidData,
	const TArray<FLossAccumulator>& TotalLoss, const TArray<FArrivalAccumulator>& TotalArrival,
	const int32 SrcIdx) const
{
	const FValidData& Data = ValidData[SrcIdx];
	const TSharedPtr<FSourceRayData>& RayData = Data.SourceData;

	FSourceResult Published;
	const FVector ToSource = Data.EmitterPos - ListenerPos;
	const float SourceDistanceCm = ToSource.Size();
	const FVector DirToSource = SourceDistanceCm > KINDA_SMALL_NUMBER
		? ToSource / SourceDistanceCm
		: FVector::ForwardVector;

	float MeanTransmission = 0.f;
	float MeanDiffraction = 0.f;
	for (int32 Band = 0; Band < RTA::NumBands; ++Band)
	{
		MeanTransmission += Data.DirectTransmissionEnergy[Band];
		MeanDiffraction  += Data.DirectDiffractionEnergy[Band];
	}
	MeanTransmission = FMath::Clamp(MeanTransmission / float(RTA::NumBands), 0.f, 1.f);
	MeanDiffraction  = FMath::Clamp(MeanDiffraction  / float(RTA::NumBands), 0.f, 1.f);
	const float MeanDirect = FMath::Min(MeanTransmission + MeanDiffraction, 1.f);

	const FVector DetourDir = Data.bHasDetour
		? (Data.DetourPoint - ListenerPos).GetSafeNormal()
		: DirToSource;

	const FArrivalAccumulator& Arrival = TotalArrival[SrcIdx];
	
	const float IndirectFraction  = Arrival.Energy / float(RayCount);
	const float DirectWeight      = MeanTransmission;
	const float DiffractionWeight = Data.bHasDetour
		? MeanDiffraction * PrecedenceWeight(Data.DetourExcessCm * RTA::CmToMetres, PrecedenceWindowMetres())
		: 0.f;
	const float IndirectWeight    = (1.f - MeanDirect) * IndirectFraction;
	const float TotalWeight       = DirectWeight + DiffractionWeight + IndirectWeight;
	
	const FVector IndirectMean = Arrival.Energy > KINDA_SMALL_NUMBER
		? Arrival.Sum / Arrival.Energy
		: FVector::ZeroVector;
	
	const FVector RawArrival = TotalWeight > KINDA_SMALL_NUMBER
		? (DirToSource * DirectWeight + DetourDir * DiffractionWeight + IndirectMean * IndirectWeight) / TotalWeight
		: DirToSource;

	const float DetourAngleDeg = Data.bHasDetour
		? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(RawArrival.GetSafeNormal(), DetourDir), -1.f, 1.f)))
		: -1.f;

	UE_LOG(LogRTA, Verbose, TEXT("SourceId=%u W[through]=%.3f W[around]=%.3f W[reflect]=%.3f AngleToDetour=%.0fdeg"),
		Data.SourceId, DirectWeight, DiffractionWeight, IndirectWeight, DetourAngleDeg);

	{
		FWriteScopeLock Lock(RayData->Lock);

		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			const float IndirectLoss = TotalLoss[SrcIdx].Bands[Band] / float(RayCount);
			const float IndirectEnergy = 1.f - IndirectLoss;
			const float DirectEnergy = FMath::Min(
				Data.DirectTransmissionEnergy[Band] + Data.DirectDiffractionEnergy[Band], 1.f);

			const float FinalEnergy = DirectEnergy + (1.f - DirectEnergy) * IndirectEnergy;
			const float NewEstimate = FMath::Clamp(1.f - FinalEnergy, 0.f, 1.f);

			RayData->DirectTransmissionLoss[Band] = RayData->bHasValidEstimate
				? FMath::Lerp(RayData->DirectTransmissionLoss[Band], NewEstimate, OcclusionSmoothingAlpha)
				: NewEstimate;
		}

		RayData->DirectLowpassCutoffHz = RayData->bHasValidEstimate
			? FMath::Lerp(RayData->DirectLowpassCutoffHz, ValidData[SrcIdx].AirAbsorptionCutoffHz, OcclusionSmoothingAlpha)
			: ValidData[SrcIdx].AirAbsorptionCutoffHz;

		RayData->SmoothedArrival = RayData->bHasValidEstimate
			? FMath::Lerp(RayData->SmoothedArrival, RawArrival, OcclusionSmoothingAlpha)
			: RawArrival;

		RayData->bHasValidEstimate = true;

		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			Published.DirectTransmissionLoss[Band] = RayData->DirectTransmissionLoss[Band];
		}
		Published.DirectLowpassCutoffHz = RayData->DirectLowpassCutoffHz;

		const float Focus = RayData->SmoothedArrival.Size();
		Published.ArrivalDirection = Focus > KINDA_SMALL_NUMBER
			? RayData->SmoothedArrival / Focus
			: DirToSource;
		Published.ArrivalFocus = FMath::Clamp(Focus, 0.f, 1.f);

		Published.VirtualPosition = ListenerPos + Published.ArrivalDirection * SourceDistanceCm;

		Published.bHasDetour = Data.bHasDetour;
		Published.DetourPoint = Data.DetourPoint;
		Published.DetourExcessCm = Data.DetourExcessCm;

		Published.bHasValidEstimate = true;
	}

	RayData->Result.Write(Published);

	const float OffAxisDeg = FMath::RadiansToDegrees(FMath::Acos(
		FMath::Clamp(FVector::DotProduct(Published.ArrivalDirection, DirToSource), -1.f, 1.f)));

	UE_LOG(LogRTA, Verbose, TEXT("SourceId=%u Loss=[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f] CutoffHz=%.0f Arrival=(%.2f,%.2f,%.2f) OffAxis=%.0fdeg Focus=%.2f Detour=%d Excess=%.0fcm Diff=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]"),
		Data.SourceId,
		Published.DirectTransmissionLoss[0], Published.DirectTransmissionLoss[1],
		Published.DirectTransmissionLoss[2], Published.DirectTransmissionLoss[3],
		Published.DirectTransmissionLoss[4], Published.DirectTransmissionLoss[5],
		Published.DirectLowpassCutoffHz,
		Published.ArrivalDirection.X, Published.ArrivalDirection.Y, Published.ArrivalDirection.Z,
		OffAxisDeg, Published.ArrivalFocus,
		Data.bHasDetour ? 1 : 0, Data.DetourExcessCm,
		Data.DirectDiffractionEnergy[0], Data.DirectDiffractionEnergy[1],
		Data.DirectDiffractionEnergy[2], Data.DirectDiffractionEnergy[3],
		Data.DirectDiffractionEnergy[4], Data.DirectDiffractionEnergy[5]);
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
		const float BiasCm = SurfaceBiasCm;

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
			Mismatches += (Fast[i] != World->LineTraceTestByChannel(Pairs[i].Key, Pairs[i].Value, ECC_Visibility, P));
		}
		UE_LOG(LogRTA, Log, TEXT("Probe trace validation: %d / %d mismatches"), Mismatches, Pairs.Num());
	}

	const float NormFactor = 1.f /
		(float(RayCount) * PI * FMath::Square(RTA::ReceiverRadiusCm));
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
			Listener->MeanFreePathMetres = float(Stats.PathLengthSumCm / Stats.HitCount) * RTA::CmToMetres;
			for (int32 Band = 0; Band < RTA::NumBands; ++Band)
			{
				Listener->MeanAbsorption[Band] = FMath::Clamp(
					float(Stats.AbsorptionSum[Band] / Stats.HitCount), 0.f, 0.99f);
			}
			ComputeEyringRT60(Listener->MeanFreePathMetres, Listener->MeanAbsorption, Listener->EyringRT60);
			Listener->bHasValidEstimate = true;
		}

		Listener->EscapedRayFraction = float(Stats.EscapedRays) / float(RayCount);
		Listener->MeanBounceDepth = float(Stats.HitCount) / float(RayCount);

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
			Published.FirstReflectionSeconds = (FirstBin > 0)
				? float(FirstBin) * FEchogram::BinWidthSeconds
				: 0.f;
		}

		Published.MeanFreePathMetres = Listener->MeanFreePathMetres;
		Published.EscapedRayFraction = Listener->EscapedRayFraction;
		Published.MeanBounceDepth = Listener->MeanBounceDepth;
		Published.bHasValidEstimate = Listener->bHasValidEstimate;
	}

	Listener->Result.Write(Published);

	{
		FReadScopeLock Lock(Listener->Lock);
		UE_LOG(LogRTA, Log,
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

	UE_LOG(LogRTA, Log, TEXT("RoomProbe shadows: %d considered, %d out of window, %d rouletted, %d traced (%.0f%% skipped)"),
		ShadowsConsidered, ShadowsOutOfWindow, ShadowsRouletted, ShadowsTraced,
		ShadowsConsidered > 0 ? 100.f * (1.f - float(ShadowsTraced) / float(ShadowsConsidered)) : 0.f);

	if (GRTAShoebox != 0)
	{
		LogShoeboxReport(Published, DecayMetrics, ProbeCountSnapshot);
	}
}

void FRaytraceManager::LogShoeboxReport(const FRoomResult& Published,
	const TStaticArray<FDecayMetric, RTA::NumBands>& DecayMetrics, int32 ProbeCount) const
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

		OutRT60[Band] = (Denominator > KINDA_SMALL_NUMBER)
			? FMath::Min(Numerator / Denominator, 20.f)
			: 20.f;
	}
}


void FRaytraceManager::DumpEchogram() const
{
	if (!Listener.IsValid()) return;
	FEchogram Snapshot;
	int64 ProbeCount = 0;
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