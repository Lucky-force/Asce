// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMovementSeparationProcessor.h"

#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassEntityManager.h"
#include "Engine/World.h"
#include "MassSubsystemAccess.h"
#include "AsceMass/Fragments/AsceMovementFragments.h"
#include "AsceMass/SpatialGrid/AsceSpatialGridSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceMovementSeparationProcessor)

UAsceMovementSeparationProcessor::UAsceMovementSeparationProcessor()
	: EntityQuery(*this)
{
	// Server和Client都要做：双方独立模拟单位位置（Client本地预测），碰撞修正要在两端一致生效
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	// 不设ExecuteInGroup（顶层），SimulationSubsystem手动按序编排，这里组名不影响手动Run。
	// 跟Steering区分开：Steering负责"想往哪走"，本Processor负责"不允许穿模"。
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
}

void UAsceMovementSeparationProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddRequirement<FAsceMovementSimStateFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UAsceSpatialGridSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceMovementSeparationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 查询期间的SpatialGrid数据是"上一帧"的（SpatialGridSubsystem在TG_PostPhysical重建），
	// 我们在这里查询到的邻居位置允许有一帧延迟，不影响穿透修正的收敛性。
	EntityQuery.ForEachEntityChunk(Context, [this, &EntityManager](FMassExecutionContext& Ctx)
	{
		UAsceSpatialGridSubsystem& SpatialGrid = Ctx.GetMutableSubsystemChecked<UAsceSpatialGridSubsystem>();
		const TArrayView<FTransformFragment> Transforms = Ctx.GetMutableFragmentView<FTransformFragment>();
		const TConstArrayView<FAgentRadiusFragment> Radii = Ctx.GetFragmentView<FAgentRadiusFragment>();
		const TConstArrayView<FMassEntityHandle> Handles = Ctx.GetEntities();

		const FMassEntityManager& ConstEntityManager = EntityManager;

		for (int32 i = 0; i < Ctx.GetNumEntities(); ++i)
		{
			const float SelfRadius = Radii[i].Radius;
			const FVector& CurrentLocation = Transforms[i].GetTransform().GetLocation();
			const FVector2D CurrentLocation2D(CurrentLocation.X, CurrentLocation.Y);

			TArray<FMassEntityHandle> Neighbors;
			SpatialGrid.FindEntities(CurrentLocation2D, QueryRadius, Neighbors);

			FVector2D SeparationPush2D = FVector2D::ZeroVector;
			for (const FMassEntityHandle& NeighborHandle : Neighbors)
			{
				if (NeighborHandle == Handles[i])
				{
					continue;
				}

				const FTransformFragment* NeighborTransform = ConstEntityManager.GetFragmentDataPtr<FTransformFragment>(NeighborHandle);
				if (!NeighborTransform)
				{
					continue;
				}
				const FAgentRadiusFragment* NeighborRadius = ConstEntityManager.GetFragmentDataPtr<FAgentRadiusFragment>(NeighborHandle);
				const float CombinedRadius = SelfRadius + (NeighborRadius ? NeighborRadius->Radius : SelfRadius);

				const FVector NeighborLocation = NeighborTransform->GetTransform().GetLocation();
				const FVector2D ToSelf2D = CurrentLocation2D - FVector2D(NeighborLocation.X, NeighborLocation.Y);
				const float Distance = ToSelf2D.Size();
				if (Distance <= KINDA_SMALL_NUMBER || Distance >= CombinedRadius)
				{
					// 不重叠就不推（Distance >= CombinedRadius）；Distance为0的退化情况也跳过
					continue;
				}

				// 只修正"穿透深度"（CombinedRadius - Distance），不适用范围内所有距离都推
				const float PenetrationDepth = CombinedRadius - Distance;
				const FVector2D Direction2D = ToSelf2D / Distance;
				SeparationPush2D += Direction2D * PenetrationDepth;
			}

			if (!SeparationPush2D.IsNearlyZero())
			{
				// 双方各推一半：自己推一半，邻居侧由邻居自己那一份处理（对称，穿透量被均摊）
				SeparationPush2D *= SeparatonRatio;

				FTransform& Transform = Transforms[i].GetMutableTransform();
				FVector NewLocation = Transform.GetLocation();
				NewLocation.X += SeparationPush2D.X;
				NewLocation.Y += SeparationPush2D.Y;
				Transform.SetTranslation(NewLocation);
			}
		}
	});
}
