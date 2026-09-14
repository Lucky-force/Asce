// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceArrivalProcessor.h"

#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassEntityHandle.h"
#include "MassMovementFragments.h"
#include "MassEntityManager.h"
#include "AsceMass/Fragments/AsceMovementFragments.h"
#include "AsceMass/Fragments/AsceOrderFragments.h"
#include "AsceMass/FlowField/AscePathFindingSubsystem.h"
#include "AsceMass/FlowField/AscePathFinder.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceArrivalProcessor)

UAsceArrivalProcessor::UAsceArrivalProcessor()
	: EntityQuery(*this)
{
	// 两端都跑：Client本地预测也要标记到达（和移动/碰撞解算一样，两端各自独立模拟，
	// 纯几何规则保证两端结果一致；没有时间量所以不会随时间漂移）。
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
}

void UAsceArrivalProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddRequirement<FAsceMovementSimStateFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FAsceMoveOrderFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
}

bool UAsceArrivalProcessor::IsWallBetween(const FWorldMapData& WorldMapData, const FVector2D& From, const FVector2D& To) const
{
	const int32 GridCountX = WorldMapData.GetGridCountX();
	const int32 GridCountY = WorldMapData.GetGridCountY();
	if (GridCountX <= 0 || GridCountY <= 0)
	{
		return false;
	}

	const FVector2D Delta = To - From;
	const float Length = Delta.Size();
	const float Step = FMath::Max(1.f, WallCheckSampleStepCm);
	const int32 SampleCount = FMath::Max(1, FMath::CeilToInt(Length / Step));

	for (int32 i = 1; i < SampleCount; ++i)   // 不含两个端点本身
	{
		const float T = static_cast<float>(i) / static_cast<float>(SampleCount);
		const FVector2D Sample = From + Delta * T;
		const FIntVector2 GridLocation = FWorldMapData::WorldLocationToGridLocation(Sample);
		if (GridLocation.X < 0 || GridLocation.Y < 0 || GridLocation.X >= GridCountX || GridLocation.Y >= GridCountY)
		{
			continue;
		}
		const int32 GridIndex = GridLocation.Y * GridCountX + GridLocation.X;
		if (WorldMapData.CostFields.IsValidIndex(GridIndex) && WorldMapData.CostFields[GridIndex].IsWall())
		{
			return true;
		}
	}
	return false;
}

void UAsceArrivalProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UAscePathFindingSubsystem* PathFindingSubsystem = UAscePathFindingSubsystem::Get(Context.GetWorld());
	UPathFinder* PathFinder = PathFindingSubsystem ? PathFindingSubsystem->GetPathFinder() : nullptr;
	if (!PathFinder)
	{
		return;
	}

	// ── A. Disruption 重置队列：按 plan 成员名单直接遍历该组（不扫世界）──
	{
		TSet<uint32> ResetPlanRawIDs;
		PathFindingSubsystem->DrainPendingPlanGroupResets(ResetPlanRawIDs);
		for (const uint32 PlanRawID : ResetPlanRawIDs)
		{
			FMergedPathPlanHandle PlanHandle;
			PlanHandle.RawID = PlanRawID;
			if (!PathFinder->IsPathPlanAlive(PlanHandle))
			{
				continue;   // 方案已结束（查不到 Handle）→ 放弃重置
			}
			const TArray<FMassEntityHandle>* Members = PathFinder->GetPlanMembers(PlanHandle);
			if (!Members)
			{
				continue;
			}
			for (const FMassEntityHandle& MemberHandle : *Members)
			{
				if (!EntityManager.IsEntityValid(MemberHandle))
				{
					continue;   // 已死亡 / index 被别的 entity 复用（EntityHandle 序列号会挡住）
				}
				FAsceMoveOrderFragment* MoveOrder = EntityManager.GetFragmentDataPtr<FAsceMoveOrderFragment>(MemberHandle);
				FAsceMovementSimStateFragment* SimState = EntityManager.GetFragmentDataPtr<FAsceMovementSimStateFragment>(MemberHandle);
				if (!MoveOrder || !SimState)
				{
					continue;
				}
				if (MoveOrder->PathPlanHandle.RawID != PlanRawID)
				{
					continue;   // 已被重新下单，不再属于本组
				}
				if (!MoveOrder->bHasOrder)
				{
					// 已到达（到达时释放了 Handle）：恢复订单状态，重新争抢位置
					MoveOrder->bHasOrder = true;
					SimState->bArrived = false;
					PathFinder->AcquirePathPlan(MoveOrder->PathPlanHandle);
				}
				// 还在移动中的成员本来就持有 Handle，不用动
			}
			// 半径清零：重新从"第一个到点的人"开始长
			PathFinder->ResetPlanArrivalRadiusSq(PlanHandle);
		}
	}

	const FWorldMapData* WorldMapData = PathFinder->GetWorldMapData();

	// ── B. 已到达单位：顶飞/离位检测 + 收集传染源 ──
	struct FArrivedSource
	{
		FVector2D Location = FVector2D::ZeroVector;
		float Radius = 0.f;
		uint32 PlanRawID = 0;
		float DistSqToTarget = 0.f;
	};
	TArray<FArrivedSource> Sources;
	TMap<FIntVector2, TArray<int32>> SourceHash;
	const float HashCellSize = FMath::Max(50.f, ContactHashCellCm);

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const TArrayView<FAsceMovementSimStateFragment> SimStates = Ctx.GetMutableFragmentView<FAsceMovementSimStateFragment>();
		const TArrayView<FAsceMoveOrderFragment> Orders = Ctx.GetMutableFragmentView<FAsceMoveOrderFragment>();
		const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FAgentRadiusFragment> Radii = Ctx.GetFragmentView<FAgentRadiusFragment>();

		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			// "已到达成员" = bArrived 且还留着有效的 Handle RawID
			// （空闲单位虽然 bArrived=true，但 RawID 是无效的，天然排除）
			if (!SimStates[i].bArrived || !Orders[i].PathPlanHandle.IsValid())
			{
				continue;
			}
			if (!PathFinder->IsPathPlanAlive(Orders[i].PathPlanHandle))
			{
				continue;   // 方案已结束：无法判半径也无法回退，维持现状
			}

			const FVector Location = Transforms[i].GetTransform().GetLocation();
			const FVector2D Location2D(Location.X, Location.Y);
			const FVector2D Target2D(Orders[i].TargetLocation.X, Orders[i].TargetLocation.Y);
			const float DistSqToTarget = FVector2D::DistSquared(Location2D, Target2D);

			const float ArrivalRadiusSq = PathFinder->GetPlanArrivalRadiusSq(Orders[i].PathPlanHandle);
			if (ArrivalRadiusSq > 0.f && DistSqToTarget > ArrivalRadiusSq)
			{
				// 被顶飞/传送出到达判定圈：回退寻路（用缓存的上次 Handle 重新 Acquire）
				SimStates[i].bArrived = false;
				Orders[i].bHasOrder = true;
				PathFinder->AcquirePathPlan(Orders[i].PathPlanHandle);
				continue;
			}

			const int32 SourceIndex = Sources.Add(FArrivedSource{
				Location2D, Radii[i].Radius, Orders[i].PathPlanHandle.RawID, DistSqToTarget });
			SourceHash.FindOrAdd(FIntVector2(
				FMath::FloorToInt32(Location2D.X / HashCellSize),
				FMath::FloorToInt32(Location2D.Y / HashCellSize))).Add(SourceIndex);
		}
	});

	// ── C. 未到达单位：路径1（无条件）+ 路径2（接触传播）──
	// 注意：即使 Sources 为空也要跑——第一个到达者靠的就是路径1。
	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const TArrayView<FAsceMovementSimStateFragment> SimStates = Ctx.GetMutableFragmentView<FAsceMovementSimStateFragment>();
		const TArrayView<FAsceMoveOrderFragment> Orders = Ctx.GetMutableFragmentView<FAsceMoveOrderFragment>();
		const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FAgentRadiusFragment> Radii = Ctx.GetFragmentView<FAgentRadiusFragment>();

		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			if (!Orders[i].bHasOrder || SimStates[i].bArrived)
			{
				continue;
			}

			const FVector Location = Transforms[i].GetTransform().GetLocation();
			const FVector2D Location2D(Location.X, Location.Y);
			const FVector2D Target2D(Orders[i].TargetLocation.X, Orders[i].TargetLocation.Y);
			const float DistSqToTarget = FVector2D::DistSquared(Location2D, Target2D);

			bool bArrived = false;

			// 路径1：自己走到了目标点。无条件、与别人是否到达无关。
			if (DistSqToTarget <= Orders[i].AcceptanceRadiusSquared)
			{
				bArrived = true;
			}
			// 路径2：进入 plan 的到达判定半径后，检测与同组已到达单位的接触
			else if (Orders[i].PathPlanHandle.IsValid() && PathFinder->IsPathPlanAlive(Orders[i].PathPlanHandle))
			{
				const float ArrivalRadiusSq = PathFinder->GetPlanArrivalRadiusSq(Orders[i].PathPlanHandle);
				if (ArrivalRadiusSq > 0.f && DistSqToTarget <= ArrivalRadiusSq)
				{
					const FIntVector2 CenterCell(
						FMath::FloorToInt32(Location2D.X / HashCellSize),
						FMath::FloorToInt32(Location2D.Y / HashCellSize));

					for (int32 DY = -1; DY <= 1 && !bArrived; ++DY)
					{
						for (int32 DX = -1; DX <= 1 && !bArrived; ++DX)
						{
							const TArray<int32>* CellSources = SourceHash.Find(FIntVector2(CenterCell.X + DX, CenterCell.Y + DY));
							if (!CellSources)
							{
								continue;
							}
							for (const int32 SourceIndex : *CellSources)
							{
								const FArrivedSource& Source = Sources[SourceIndex];

								// 同一批订单（同一个 plan）
								if (Source.PlanRawID != Orders[i].PathPlanHandle.RawID)
								{
									continue;
								}
								// 源不能比我们远（方向单调：只从目标向外一层层传播）
								if (Source.DistSqToTarget > DistSqToTarget)
								{
									continue;
								}
								// 接触（圆重叠 + 容差）
								const float ContactDist = (Source.Location - Location2D).Size();
								if (ContactDist > Radii[i].Radius + Source.Radius + ContactToleranceCm)
								{
									continue;
								}
								// 隔墙的"假接触"
								if (WorldMapData && IsWallBetween(*WorldMapData, Location2D, Source.Location))
								{
									continue;
								}

								bArrived = true;
								break;
							}
						}
					}
				}
			}

			if (!bArrived)
			{
				continue;
			}

			// 到达动作：先长半径（Release 可能直接回收 plan），再清订单。
			// 刻意保留 Orders[i].PathPlanHandle 的 RawID——顶飞回退 / disruption 组重置还要用。
			const float GrowRadius = FMath::Sqrt(DistSqToTarget) + 2.f * Radii[i].Radius + ArrivalRadiusMarginCm;
			PathFinder->GrowPlanArrivalRadiusSq(Orders[i].PathPlanHandle, GrowRadius * GrowRadius);

			SimStates[i].bArrived = true;
			Orders[i].bHasOrder = false;
			PathFinder->ReleasePathPlan(Orders[i].PathPlanHandle);
		}
	});
}
