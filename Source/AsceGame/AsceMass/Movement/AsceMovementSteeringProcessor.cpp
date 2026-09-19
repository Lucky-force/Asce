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
	// 动态处理器：由 UAsceMassMovementSimulationSubsystem 手动按序跑（调度器是唯一时钟）。
	// 默认 true 会被 MassSimulation 自动链再跑一份，且绕过世界初始化/loading 门控。
	bAutoRegisterWithProcessingPhases = false;
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
	EntityQuery.AddRequirement<FAsceMovementTuningFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassDesiredMovementFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddSubsystemRequirement<UAscePathFindingSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceMovementSteeringProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// UAscePathFindingSubsystem标记了GameThreadOnly，这里不用ParallelForEachEntityChunk。
	EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FAsceMovementSimStateFragment> SimStates = Ctx.GetFragmentView<FAsceMovementSimStateFragment>();
		const TArrayView<FAsceMoveOrderFragment> Orders = Ctx.GetMutableFragmentView<FAsceMoveOrderFragment>();
		const TConstArrayView<FAsceMovementTuningFragment> Tunings = Ctx.GetFragmentView<FAsceMovementTuningFragment>();
		const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();
		const TArrayView<FMassDesiredMovementFragment> DesiredMovements = Ctx.GetMutableFragmentView<FMassDesiredMovementFragment>();

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

			// 无订单（含未下单/已到达/订单被拒绝）：不出力。
			// 注意：到达判定和"清订单"完全不在这个Processor里做——见 UAsceArrivalProcessor。
			if (!Order.bHasOrder)
			{
				DesiredMovement.DesiredVelocity = FVector::ZeroVector;
				continue;
			}

			const FVector CurrentLocation = Transforms[i].GetTransform().GetLocation();
			const FVector2D CurrentLocation2D(CurrentLocation.X, CurrentLocation.Y);

			// 方向选择：
			// ① 近目标（曼哈顿距离目标 ≤ 一格）→ 无视流场，直接朝目标点直走——
			//    （流场栅格的"每格固定一个方向"问题：不同进入方的entity跟那格的单一方向可能
			//     永远够不到格内的具体目标点；直走保证最终到位）
			// ② 否则 → 流场方向
			FVector2D SteeringDirection2D = FVector2D::ZeroVector;
			const FVector2D TargetLocation2D(Order.TargetLocation.X, Order.TargetLocation.Y);
			const FVector2D ToTarget2D = TargetLocation2D - CurrentLocation2D;
			// 曼哈顿距离（无开方，判"是否已在一格内"足够）
			const float ManhattanDistToTarget = FMath::Abs(ToTarget2D.X) + FMath::Abs(ToTarget2D.Y);
			if (ManhattanDistToTarget <= GRID_SIZE)   // 100cm（一个流场格）以内直走
			{
				SteeringDirection2D = ToTarget2D;
			}
			else
			{
				// 流场查询：Handle在指令下发时(ApplyMergedMoveOrder)就已分配好并Acquire，
				// 这里只做查询。查询失败=目标不可达或者跑出了这批请求的规划范围——
				// 不重新请求（重发同款请求结果必然相同，反而每帧产生请求风暴），
				// 原地停下，等玩家重新下单（新指令会经OrderChannel重新分配Handle）。
				if (Order.PathPlanHandle.IsValid())
				{
					PathFinder->TryGetFlowDirection(Order.PathPlanHandle, CurrentLocation2D, SteeringDirection2D);
				}
			}

			if (!SteeringDirection2D.IsNearlyZero())
			{
				SteeringDirection2D.Normalize();
			}

			const float DesiredSpeed = FMath::Min(Tunings[i].DefaultDesiredSpeed, Tunings[i].MaxSpeed);
			DesiredMovement.DesiredVelocity = FVector(SteeringDirection2D.X, SteeringDirection2D.Y, 0.f) * DesiredSpeed;
		}
	});
}
