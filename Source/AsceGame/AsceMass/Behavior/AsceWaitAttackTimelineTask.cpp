// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AsceWaitAttackTimelineTask.h"

#include "Engine/World.h"
#include "MassStateTreeDependency.h"
#include "MassStateTreeExecutionContext.h"
#include "MassStateTreeTypes.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

#include "AsceMass/Behavior/AsceAnimCueLibrary.h"
#include "AsceMass/Behavior/AsceBehaviorFragment.h"
#include "AsceMass/Behavior/AsceTimelineLibrary.h"
#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Data/AsceUnitDef.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceWaitAttackTimelineTask)

FAsceWaitAttackTimelineTask::FAsceWaitAttackTimelineTask()
{
	bShouldCallTick = true;
}

bool FAsceWaitAttackTimelineTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(TimelineHandle);
	return true;
}

void FAsceWaitAttackTimelineTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadOnly(TimelineHandle);
}

EStateTreeRunStatus FAsceWaitAttackTimelineTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.bLoggedStuck = false;

	const UWorld* World = Context.GetWorld();
	const FAsceBehaviorFragment* Behavior = Context.GetExternalDataPtr(TimelineHandle);
	if (!World || !Behavior || !Behavior->IsTimelineActive())
	{
		Data.TargetTime = 0.f;
		UE_LOG(LogTemp, Verbose, TEXT("[WaitAttack] 无时间轴 ⇒ 直接通过"));
		return EStateTreeRunStatus::Succeeded;
	}

	const FMassStateTreeExecutionContext& MassContext = static_cast<const FMassStateTreeExecutionContext&>(Context);
	FAsceRTSUnitDef UnitDef;
	const bool bHasUnitDef = AsceAnimCue::GetUnitDef(MassContext, UnitDef);

	float BaseTime = Data.FallbackTime;
	float BaseAttackSpeed = 180.f;
	if (bHasUnitDef)
	{
		if (UnitDef.BaseAttackTime > 0.f)
		{
			BaseTime = UnitDef.BaseAttackTime;
		}
		if (UnitDef.BaseAttackSpeed > 0.f)
		{
			BaseAttackSpeed = UnitDef.BaseAttackSpeed;
		}
	}

	float Rate = 1.f;
	if (Data.bApplyAttackSpeed)
	{
		if (const FAsceCombatAttributesFragment* Combat = MassContext.GetEntityManager()
			.GetFragmentDataPtr<FAsceCombatAttributesFragment>(MassContext.GetEntity()))
		{
			Rate = Combat->AttackSpeed / BaseAttackSpeed;
		}
	}

	const float Ratio = FMath::Max(Data.TimeRatio, 0.01f);
	const float Elapsed = AsceTimeline::GetTime(*World, *Behavior);
	// 本段时长从进入节点起算（不是动作起点的绝对值）；前摇/后摇可以安全拆成 0.7 / 0.3
	Data.TargetTime = Elapsed + (BaseTime * Ratio) / FMath::Clamp(Rate, 0.05f, 20.f);

	const bool bReached = Elapsed >= Data.TargetTime;
	UE_LOG(LogTemp, Verbose, TEXT("[WaitAttack] Base=%.2f Ratio=%.2f Rate=%.2f Target=%.2f Elapsed=%.2f ⇒ %s"),
		BaseTime, Ratio, Rate, Data.TargetTime, Elapsed, bReached ? TEXT("已到达") : TEXT("等待中"));
	return bReached ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FAsceWaitAttackTimelineTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	const UWorld* World = Context.GetWorld();
	const FAsceBehaviorFragment* Behavior = Context.GetExternalDataPtr(TimelineHandle);
	if (!World || !Behavior || !Behavior->IsTimelineActive())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	const float Elapsed = AsceTimeline::GetTime(*World, *Behavior);
	if (Elapsed >= Data.TargetTime)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[WaitAttack] 到达 Target=%.2f Elapsed=%.2f"), Data.TargetTime, Elapsed);
		return EStateTreeRunStatus::Succeeded;
	}
	if (!Data.bLoggedStuck && Elapsed >= 3.f)
	{
		Data.bLoggedStuck = true;
		UE_LOG(LogTemp, Verbose, TEXT("[WaitAttack] 等待超 3s：Target=%.2f Elapsed=%.2f"), Data.TargetTime, Elapsed);
	}
	return EStateTreeRunStatus::Running;
}
