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
	EntityQuery.AddRequirement<FAsceMovementSimStateFragment>(EMassFragmentAccess::ReadWrite);
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
		const TArrayView<FAsceMovementSimStateFragment> SimStates = Ctx.GetMutableFragmentView<FAsceMovementSimStateFragment>();
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

		// "位移过小→视为到达"只在权威端判定（Standalone/Server）——客户端的速度/位置是复制值，
		// 本地不做停止判定（否则会与服务器不一致）；客户端等服务器清Order后跟随。
		const UWorld* SteeringWorld = Ctx.GetWorld();
		const bool bIsAuthority = !SteeringWorld || SteeringWorld->GetNetMode() != NM_Client;

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

			// 无条件记录本帧位置（所有continue都会跳过，必须放最前）+ 先存上帧值供位移判定。
			// 注意先后顺序：先取上帧值，再覆盖成本帧值——位移判定用的是"上帧→本帧"的真实位移。
			const FVector2D PrevFrameLocation2D = SimStates[i].LastFrameLocation2D;
			SimStates[i].LastFrameLocation2D = CurrentLocation2D;

			if (!Order.bHasOrder)
			{
				DesiredMovement.DesiredVelocity = FVector::ZeroVector;
				SimStates[i].StuckFrames = 0;
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
				SimStates[i].StuckFrames = 0;
				continue;
			}

			// ── 权威端："位移过小→视为到达"（挤死/堵死时停）──
			// 单位被大规模移动挤在目标外围挤不动（实际位移远小于期望位移）时，
			// 判定为"事实到达"（不是预测——依据本帧实际位移）：
			// 连续StuckStopFrameCount帧，在"期望移动方向上"的位移投影 < 期望位移×阈值 → 清Order停。
			// 用投影而不是纯长度：被 CollisionSolver 推开（反向位移）不会污染判定
			// （反向投影为负也不计入"动了"），只有"真想走但被挤住"时投影才≈0。
			// 临时阻挡（几帧）→ 计数器恢复；真挤死30帧后停。
			// 客户端不做（速度/位置是复制值，判定会与服务端不一致）。
			if (bIsAuthority)
			{
				const float DeltaTime = Ctx.GetDeltaTimeSeconds();
				const float DesiredSpeed = FMath::Min(MovementParams.DefaultDesiredSpeed, MovementParams.MaxSpeed);
				const float ExpectedMovePerFrame = DesiredSpeed * DeltaTime;   // 期望位移（这帧应走多远）

				// 期望方向 = 本帧期望移动方向（用上帧DesiredVelocity方向：它代表"一直想走的流场方向"，
				// 而不是目标直线方向——流场绕障碍/转角时，直线方向不代表前进方向，投影会误判）。
				// 挤死时单位"朝期望方向走但被堵住" → 位移在该方向上的投影≈0 → 判停。
				const FVector2D ExpectedDir2D(DesiredMovement.DesiredVelocity.X, DesiredMovement.DesiredVelocity.Y);
				if (ExpectedMovePerFrame > KINDA_SMALL_NUMBER)
				{
					if (ExpectedDir2D.IsNearlyZero())
					{
						// 上帧DesiredVelocity为0（刚起步/查失败）——防御性重置
						SimStates[i].StuckFrames = 0;
					}
					else
					{
						const FVector2D ExpectedDir = ExpectedDir2D.GetSafeNormal();
						const FVector2D ActualMovePerFrame = CurrentLocation2D - PrevFrameLocation2D;   // 上帧→本帧真实位移（含被推开）
						const float ActualAlongExpected = ActualMovePerFrame.Dot(ExpectedDir);   // 期望方向上的实际位移
						if (ActualAlongExpected < ExpectedMovePerFrame * StuckMoveThresholdRatio)
						{
							// 这帧在期望方向上的位移 < 期望位移的StuckMoveThresholdRatio倍
							if (++SimStates[i].StuckFrames >= StuckStopFrameCount)
							{
								// 连续30帧挤死 → 视为到达：清Order停
								if (Order.PathPlanHandle.IsValid())
								{
									PathFinder->ReleasePathPlan(Order.PathPlanHandle);
									Order.PathPlanHandle = FMergedPathPlanHandle();
								}
								Order.bHasOrder = false;
								DesiredMovement.DesiredVelocity = FVector::ZeroVector;
								SimStates[i].StuckFrames = 0;
								continue;
							}
						}
						else
						{
							SimStates[i].StuckFrames = 0;   // 有正常前进位移，重置
						}
					}
				}
				else
				{
					SimStates[i].StuckFrames = 0;   // 期望速度退化，重置
				}
			}
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

			const float DesiredSpeed = FMath::Min(MovementParams.DefaultDesiredSpeed, MovementParams.MaxSpeed);
			DesiredMovement.DesiredVelocity = FVector(SteeringDirection2D.X, SteeringDirection2D.Y, 0.f) * DesiredSpeed;
		}
	});
}
