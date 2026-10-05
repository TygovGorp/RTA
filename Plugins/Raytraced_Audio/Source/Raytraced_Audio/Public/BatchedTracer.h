#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "PBDRigidsSolver.h"
#include "CollisionQueryFilterCallbackCore.h"
#include "ChaosInterfaceWrapperCore.h"
#include "Physics/Experimental/ChaosInterfaceWrapper.h"

class FRTABatchedTracer
{
public:
	FRTABatchedTracer(UWorld* InWorld, bool bTraceComplex);
	~FRTABatchedTracer();

	UE_NONCOPYABLE(FRTABatchedTracer);

	bool IsValid() const { return Accel != nullptr; }

	bool TraceTest(const FVector& Start, const FVector& End) const;

private:
	class FFilterCallback final : public ICollisionQueryFilterCallback
	{
	public:
		virtual ECollisionQueryHitType PreFilter(const FQueryFilterData& FilterData,
			const Chaos::FPerShapeData& Shape, const Chaos::FGeometryParticle&) override
		{
			return Filter(FilterData, Shape);
		}

		virtual ECollisionQueryHitType PreFilter(const FQueryFilterData& FilterData,
			const Chaos::FPerShapeData& Shape, const Chaos::FGeometryParticleHandle&) override
		{
			return Filter(FilterData, Shape);
		}

		// Not invoked: the query flags are PreFilter | AnyHit with no PostFilter.
		virtual ECollisionQueryHitType PostFilter(const FQueryFilterData&,
			const ChaosInterface::FQueryHit&) override { return ECollisionQueryHitType::Block; }
		virtual ECollisionQueryHitType PostFilter(const FQueryFilterData&,
			const ChaosInterface::FPTQueryHit&) override { return ECollisionQueryHitType::Block; }

	private:
		static ECollisionQueryHitType Filter(const FQueryFilterData& FilterData, const Chaos::FPerShapeData& Shape)
		{
			return FilterData.NarrowFilter(Shape.GetShapeFilterData()) == Chaos::Filter::ENarrowFilterResult::Block
				? ECollisionQueryHitType::Block
				: ECollisionQueryHitType::None;
		}
	};

	FFilterCallback Callback;
	TUniquePtr<ChaosInterface::FSceneQueryCommonParams> CommonParams;

	const Chaos::IDefaultChaosSpatialAcceleration* Accel = nullptr;

	Chaos::FPhysicsSolver* LockedSolver = nullptr;
};