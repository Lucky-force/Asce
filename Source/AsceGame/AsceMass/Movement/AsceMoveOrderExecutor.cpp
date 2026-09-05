// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMoveOrderExecutor.h"

#include "MassEntityManager.h"
#include "AsceMass/FlowField/AscePathFinder.h"
#include "AsceMass/Fragments/AsceOrderFragments.h"

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

	for (const FMassEntityHandle& Handle : ValidHandles)
	{
		FAsceMoveOrderFragment* MoveOrder = EntityManager.GetFragmentDataPtr<FAsceMoveOrderFragment>(Handle);
		if (!MoveOrder)
		{
			// 调用前提要求调用方保证有效性，这里出现说明中途entity状态变了（防御性判断，不应发生）。
			continue;
		}

		if (MoveOrder->PathPlanHandle.IsValid())
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
	}
}
