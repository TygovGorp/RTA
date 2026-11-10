#include "BatchedTracer.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Log.h"

namespace
{
	/** Equivalent of CreateChaosQueryFilterData for ECC_Visibility with default responses. */
	Chaos::Filter::FQueryFilterData BuildVisibilityFilter(bool bTraceComplex)
	{
		using namespace Chaos::Filter;

		uint64 AllChannels = 0;
		for (int32 Channel = 0; Channel < 32; ++Channel)
		{
			AllChannels |= (uint64(1) << Channel);
		}

		FQueryTraceFilterBuilder Builder;
		Builder.SetCollisionChannelIndex(static_cast<uint8>(ECC_Visibility));
		Builder.SetBlockChannelMask(AllChannels);
		Builder.SetFilterFlags(bTraceComplex ? Chaos::EFilterFlags::ComplexCollision : Chaos::EFilterFlags::SimpleCollision, true);
		return Builder.Build();
	}
}

FRTABatchedTracer::FRTABatchedTracer(UWorld* InWorld, bool bTraceComplex)
{
	FPhysScene* Scene = InWorld ? InWorld->GetPhysicsScene() : nullptr;
	Chaos::FPhysicsSolver* Solver = Scene ? Scene->GetSolver() : nullptr;
	if (!Solver) return;

	Solver->GetExternalDataLock_External().ReadLock();
	LockedSolver = Solver;

	Accel = Scene->GetSpacialAcceleration(); 
	if (!Accel)
	{
		UE_LOG(LogRTA, Warning, TEXT("BatchedTracer: no acceleration structure"));
		return;
	}

	// TSQTraits<Raycast, Test>::GetQueryFlags()
	CommonParams = MakeUnique<ChaosInterface::FSceneQueryCommonParams>(
		Callback, BuildVisibilityFilter(bTraceComplex), EQueryFlags::PreFilter | EQueryFlags::AnyHit);
}

FRTABatchedTracer::~FRTABatchedTracer()
{
	CommonParams.Reset();
	if (LockedSolver)
	{
		LockedSolver->GetExternalDataLock_External().ReadUnlock();
	}
}

bool FRTABatchedTracer::TraceTest(const FVector& Start, const FVector& End) const
{
	const FVector Delta = End - Start;
	const double Length = Delta.Size();
	if (Length <= UE_DOUBLE_SMALL_NUMBER) return false;

	ChaosInterface::FSQSingleHitBuffer<FHitRaycast> HitBuffer;

	Chaos::Private::LowLevelRaycast(*Accel, Start, Delta / Length, static_cast<float>(Length),
		HitBuffer, EHitFlags::None, *CommonParams);

	return GetHasBlock(HitBuffer);
}