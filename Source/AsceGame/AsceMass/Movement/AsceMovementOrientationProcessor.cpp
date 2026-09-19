// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMovementOrientationProcessor.h"

#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassMovementFragments.h"

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
}

void UAsceMovementOrientationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ParallelForEachEntityChunk(Context, [this](FMassExecutionContext& Ctx)
	{
		const TArrayView<FTransformFragment> Transforms = Ctx.GetMutableFragmentView<FTransformFragment>();
		const TConstArrayView<FMassVelocityFragment> Velocities = Ctx.GetFragmentView<FMassVelocityFragment>();

		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			const FVector& Velocity = Velocities[i].Value;
			const FVector2D Velocity2D(Velocity.X, Velocity.Y);
			if (Velocity2D.SizeSquared() < FMath::Square(MinSpeedToUpdateFacing))
			{
				continue;
			}

			FTransform& CurrentTransform = Transforms[i].GetMutableTransform();
			const FVector2D VelocityDir = Velocity2D.GetSafeNormal();
			if (VelocityDir.IsNearlyZero())
			{
				continue; // 速度方向退化，保留当前朝向
			}

			const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(VelocityDir.Y, VelocityDir.X));
			const float CurrentYaw = CurrentTransform.GetRotation().Rotator().Yaw;
			const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw);
			const float MaxTurnThisFrame = TurnSpeedDegreesPerSecond * Ctx.GetDeltaTimeSeconds();
			const float NewYaw = CurrentYaw + FMath::Clamp(DeltaYaw, -MaxTurnThisFrame, MaxTurnThisFrame);
			CurrentTransform.SetRotation(FRotator(0.f, NewYaw, 0.f).Quaternion());
		}
	});
}
