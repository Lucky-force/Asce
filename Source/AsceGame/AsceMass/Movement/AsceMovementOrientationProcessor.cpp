// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMovementOrientationProcessor.h"

#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassMovementFragments.h"
#include "AsceMass/Combat/AsceCombatFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceMovementOrientationProcessor)

UAsceMovementOrientationProcessor::UAsceMovementOrientationProcessor()
	: EntityQuery(*this)
{
	// 动态处理器：由 UAsceMassMovementSimulationSubsystem 手动按序跑（调度器是唯一时钟）；
	// 默认 true 会被 MassSimulation 自动链再跑一份，且绕过 world init/loading 门控。
	bAutoRegisterWithProcessingPhases = false;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	// 不设ExecuteInGroup（留在默认的顶层），只声明"必须等Movement组全部跑完再执行"——
	// 跟引擎自带UMassApplyForceProcessor用ExecuteAfter引用Avoidance组名是同一种写法。
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
}

void UAsceMovementOrientationProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	// 动作态（可选）：Attack/Casting/Channel 期间朝向由"动作态复制通道"下发的 FacingYaw 决定
	EntityQuery.AddRequirement<FAsceCombatVisualFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
}

void UAsceMovementOrientationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ParallelForEachEntityChunk(Context, [this](FMassExecutionContext& Ctx)
	{
		const TArrayView<FTransformFragment> Transforms = Ctx.GetMutableFragmentView<FTransformFragment>();
		const TConstArrayView<FMassVelocityFragment> Velocities = Ctx.GetFragmentView<FMassVelocityFragment>();
		const TConstArrayView<FAsceCombatVisualFragment> Visuals = Ctx.GetFragmentView<FAsceCombatVisualFragment>();
		const float DeltaTime = Ctx.GetDeltaTimeSeconds();

		auto TurnToYaw = [this, DeltaTime](FTransform& Transform, const float TargetYaw)
		{
			const float CurrentYaw = Transform.GetRotation().Rotator().Yaw;
			const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw);
			const float MaxTurnThisFrame = TurnSpeedDegreesPerSecond * DeltaTime;
			Transform.SetRotation(FRotator(0.f, CurrentYaw + FMath::Clamp(DeltaYaw, -MaxTurnThisFrame, MaxTurnThisFrame), 0.f).Quaternion());
		};

		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			// 动作朝向优先：攻击/施法期间朝向由服务器定死（客户端读动作态通道；服务器端该fragment
			// 保持默认Idle，所以服务器行为不变——它本来就由 UAsceCombatProcessor 直接SetRotation）。
			if (Visuals.Num() == Num)
			{
				const EAsceActionState ActionState = Visuals[i].ActionState;
				if (ActionState == EAsceActionState::Attack
					|| ActionState == EAsceActionState::Casting
					|| ActionState == EAsceActionState::Channel)
				{
					TurnToYaw(Transforms[i].GetMutableTransform(), Visuals[i].FacingYaw);
					continue;
				}
			}

			const FVector& Velocity = Velocities[i].Value;
			const FVector2D Velocity2D(Velocity.X, Velocity.Y);
			if (Velocity2D.SizeSquared() < FMath::Square(MinSpeedToUpdateFacing))
			{
				continue;
			}

			const FVector2D VelocityDir = Velocity2D.GetSafeNormal();
			if (VelocityDir.IsNearlyZero())
			{
				continue; // 速度方向退化，保留当前朝向
			}

			const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(VelocityDir.Y, VelocityDir.X));
			TurnToYaw(Transforms[i].GetMutableTransform(), TargetYaw);
		}
	});
}
