// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMovementCorrectionProcessor.h"

#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "AsceMass/Fragments/AsceMovementFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceMovementCorrectionProcessor)

UAsceMovementCorrectionProcessor::UAsceMovementCorrectionProcessor()
	: EntityQuery(*this)
{
	// 动态处理器：由 UAsceMassMovementSimulationSubsystem 手动按序跑（调度器是唯一时钟）；
	// 默认 true 会被 MassSimulation 自动链再跑一份，且绕过 world init/loading 门控。
	bAutoRegisterWithProcessingPhases = false;
	// 只在Client执行：Server是权威，不存在"跟服务器位置的偏差"需要修正。
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Client);
	// 跟UAsceMovementOrientationProcessor一样，只声明"必须等Movement组跑完"，
	// 不设ExecuteInGroup（留在默认顶层）。
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
}

void UAsceMovementCorrectionProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FAscePositionCorrectionFragment>(EMassFragmentAccess::ReadWrite);
}

void UAsceMovementCorrectionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const float DeltaTime = Context.GetDeltaTimeSeconds();

	EntityQuery.ParallelForEachEntityChunk(Context, [this, DeltaTime](FMassExecutionContext& Ctx)
	{
		const TArrayView<FTransformFragment> Transforms = Ctx.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FAscePositionCorrectionFragment> Corrections = Ctx.GetMutableFragmentView<FAscePositionCorrectionFragment>();

		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			FAscePositionCorrectionFragment& Correction = Corrections[i];
			if (!Correction.bCorrectionPending)
			{
				continue;
			}

			FTransform& CurrentTransform = Transforms[i].GetMutableTransform();
			const FVector NewPosition = FMath::VInterpConstantTo(CurrentTransform.GetLocation(), Correction.TargetPosition, DeltaTime, CorrectionSpeed);
			CurrentTransform.SetLocation(NewPosition);

			if (NewPosition.Equals(Correction.TargetPosition))
			{
				// VInterpConstantTo到达终点时会精确等于目标值（不会像VInterpTo那样永远差一点），
				// 到了就清掉标记，之后这个Fragment不会再被写，直到下一次网络更新触发新的修正。
				Correction.bCorrectionPending = false;
			}
		}
	});
}
