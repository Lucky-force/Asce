// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMoveOrderExecutor.h"

#include "MassEntityManager.h"
#include "AsceMass/FlowField/AscePathFinder.h"
#include "AsceMass/Fragments/AsceOrderFragments.h"
#include "AsceMass/Fragments/AsceMovementFragments.h"

void FAsceMoveOrderExecutor::ApplyMergedMoveOrder(
	FMassEntityManager& EntityManager,
	UPathFinder& PathFinder,
	const TArray<FMassEntityHandle>& ValidHandles,
	const TArray<FVector2D>& EntityWorldLocations2D,
	const FVector2D& GoalWorldLocation,
	float AcceptanceRadiusSquared,
	int32 OrderSequence)
{
	if (ValidHandles.Num() == 0)
	{
		return;
	}

	const FMergedPathPlanHandle PlanHandle = PathFinder.RunMergeablePathRequest(GoalWorldLocation, EntityWorldLocations2D);

	// 登记组成员：disruption（传送/死亡等）事件后，到达处理器按这份名单直接遍历重置整组
	PathFinder.RegisterPlanMembers(PlanHandle, ValidHandles);

	for (const FMassEntityHandle& Handle : ValidHandles)
	{
		FAsceMoveOrderFragment* MoveOrder = EntityManager.GetFragmentDataPtr<FAsceMoveOrderFragment>(Handle);
		if (!MoveOrder)
		{
			// 调用前提要求调用方保证有效性，这里出现说明中途entity状态变了（防御性判断，不应发生）。
			continue;
		}

		// 注意：已到达的订单在到达时已经 Release 过（Handle 只保留 RawID），
		// 所以只有 bHasOrder==true（订单还在执行中）才需要/才能 Release，否则会重复释放。
		if (MoveOrder->bHasOrder && MoveOrder->PathPlanHandle.IsValid())
		{
			PathFinder.ReleasePathPlan(MoveOrder->PathPlanHandle);
		}
		PathFinder.AcquirePathPlan(PlanHandle);
		MoveOrder->PathPlanHandle = PlanHandle;

		// Z没有游戏玩法含义（到达判定/寻路都只看XY，见AsceMovementSteeringProcessor），固定写0。
		MoveOrder->TargetLocation = FVector(GoalWorldLocation.X, GoalWorldLocation.Y, 0.f);
		// Fragment存的是平方值，调用方传入的也必须是半径的平方（见AsceOrderFragments.h说明）。
		MoveOrder->AcceptanceRadiusSquared = AcceptanceRadiusSquared;
		MoveOrder->bHasOrder = true;
		MoveOrder->OrderSequence = OrderSequence;

		// 新订单 = 进入"移动订单执行中"状态（见 FAsceMovementSimStateFragment::bArrived）
		if (FAsceMovementSimStateFragment* SimState = EntityManager.GetFragmentDataPtr<FAsceMovementSimStateFragment>(Handle))
		{
			SimState->bArrived = false;
		}
	}
}
