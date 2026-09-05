// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMovementSteeringProcessor.h"

#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassMovementFragments.h"
#include "AsceMass/Fragments/AsceMovementFragments.h"
#include "AsceMass/Fragments/AsceOrderFragments.h"
#include "AsceMass/FlowField/AscePathFindingSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceMovementSteeringProcessor)

UAsceMovementSteeringProcessor::UAsceMovementSteeringProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	// 挂在Avoidance组：引擎自带的UMassApplyForceProcessor(ApplyForces组)/UMassApplyMovementProcessor
	// (Movement组)分别声明了ExecuteAfter(Avoidance)/ExecuteAfter(ApplyForces)，
	// 我们只要占住Avoidance这个组名，就自然排在它们前面，不需要显式声明ExecuteBefore。
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Avoidance;
}

void UAsceMovementSteeringProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddRequirement<FAsceMovementSimStateFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAsceMoveOrderFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassDesiredMovementFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddConstSharedRequirement<FMassMovementParameters>(EMassFragmentPresence::All);
	EntityQuery.AddSubsystemRequirement<UAscePathFindingSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceMovementSteeringProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// UAscePathFindingSubsystem标记了GameThreadOnly，这里不用ParallelForEachEntityChunk。
	EntityQuery.ForEachEntityChunk(Context, [this, &EntityManager](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FAsceMovementSimStateFragment> SimStates = Ctx.GetFragmentView<FAsceMovementSimStateFragment>();
		const TArrayView<FAsceMoveOrderFragment> Orders = Ctx.GetMutableFragmentView<FAsceMoveOrderFragment>();
		const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();
		const TArrayView<FMassDesiredMovementFragment> DesiredMovements = Ctx.GetMutableFragmentView<FMassDesiredMovementFragment>();
		const FMassMovementParameters& MovementParams = Ctx.GetConstSharedFragment<FMassMovementParameters>();

		UAscePathFindingSubsystem& PathFindingSubsystem = Ctx.GetMutableSubsystemChecked<UAscePathFindingSubsystem>();
		UPathFinder* PathFinder = PathFindingSubsystem.GetPathFinder();
		if (!PathFinder)
		{
			return;
		}

		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			// 战争迷雾/占位逻辑判定"不值得跑完整模拟"的entity，直接跳过——
			// DesiredVelocity维持网络同步下来的值不变，靠引擎自带的Movement组做Dead Reckoning外推。
			if (!SimStates[i].bRunFullSimulation)
			{
				continue;
			}

			FAsceMoveOrderFragment& Order = Orders[i];
			FMassDesiredMovementFragment& DesiredMovement = DesiredMovements[i];
			const FVector CurrentLocation = Transforms[i].GetTransform().GetLocation();
			const FVector2D CurrentLocation2D(CurrentLocation.X, CurrentLocation.Y);

			if (!Order.bHasOrder)
			{
				DesiredMovement.DesiredVelocity = FVector::ZeroVector;
				continue;
			}

			// 到达判定只看XY——高度目前只影响表现（以后每个格子的高度数据也只用于entity
			// 位置的Z轴表现，不参与任何寻路/玩法判定），TargetLocation.Z没有游戏玩法含义。
			// 用平方距离比较，省掉一次开方；AcceptanceRadiusSquared本身存的就是平方值，
			// 这里也不需要再算一次FMath::Square。
			const float DistanceSqToTarget = FVector2D::DistSquared(CurrentLocation2D, FVector2D(Order.TargetLocation.X, Order.TargetLocation.Y));
			if (DistanceSqToTarget <= Order.AcceptanceRadiusSquared)
			{
				// 到达目标：清空Order，释放寻路方案句柄
				if (Order.PathPlanHandle.IsValid())
				{
					PathFinder->ReleasePathPlan(Order.PathPlanHandle);
					Order.PathPlanHandle = FMergedPathPlanHandle();
				}
				Order.bHasOrder = false;
				DesiredMovement.DesiredVelocity = FVector::ZeroVector;
				continue;
			}

			// 查询方向：Handle在指令下发时(ApplyMergedMoveOrder)就已分配好并Acquire，
			// 这里只做查询。查询失败=目标不可达或者跑出了这批请求的规划范围——
			// 不重新请求（重发同款请求结果必然相同，反而每帧产生请求风暴），
			// 原地停下，等玩家重新下单（新指令会经OrderChannel重新分配Handle）。
			FVector2D FlowDirection2D = FVector2D::ZeroVector;
			if (Order.PathPlanHandle.IsValid())
			{
				PathFinder->TryGetFlowDirection(Order.PathPlanHandle, CurrentLocation2D, FlowDirection2D);
			}
			
			FVector2D SteeringDirection2D = FlowDirection2D;

			if (!SteeringDirection2D.IsNearlyZero())
			{
				SteeringDirection2D.Normalize();
			}

			const float DesiredSpeed = FMath::Min(MovementParams.DefaultDesiredSpeed, MovementParams.MaxSpeed);
			DesiredMovement.DesiredVelocity = FVector(SteeringDirection2D.X, SteeringDirection2D.Y, 0.f) * DesiredSpeed;
		}
	});
}
