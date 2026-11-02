// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AsceWaitTimelineTask.h"

#include "Engine/World.h"
#include "MassStateTreeDependency.h"   // FStateTreeDependencyBuilder 的定义
#include "MassStateTreeExecutionContext.h"
#include "MassStateTreeTypes.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

#include "AsceMass/Behavior/AsceBehaviorFragment.h"
#include "AsceMass/Behavior/AsceTimelineLibrary.h"
#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "AsceMass/Data/AsceUnitDef.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceWaitTimelineTask)

FAsceWaitTimelineTask::FAsceWaitTimelineTask()
{
	bShouldCallTick = true;
}

bool FAsceWaitTimelineTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(TimelineHandle);
	Linker.LinkExternalData(UnitDefHandle);
	return true;
}

void FAsceWaitTimelineTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	// 只读：时间轴标量由 Notify processor 写，基础攻速/施法速度来自 def
	Builder.AddReadOnly(TimelineHandle);
	Builder.AddReadOnly(UnitDefHandle);
}

float FAsceWaitTimelineTask::ResolveTimeScale(FStateTreeExecutionContext& Context, const FInstanceDataType& Data) const
{
	if (Data.ScaleMode == EAsceTimelineScaleMode::Constant)
	{
		return 1.f;
	}

	const FAsceUnitDefFragment* UnitDefFrag = Context.GetExternalDataPtr(UnitDefHandle);
	if (!UnitDefFrag)
	{
		return 1.f;
	}

	FAsceEntityDefRow Row;
	if (!UAsceEntityDefLibrary::GetEntityDefRow(UnitDefFrag->UniqueTypeId, Row))
	{
		return 1.f;
	}
	const FAsceRTSUnitDef* UnitDef = Row.Def.GetPtr<FAsceRTSUnitDef>();
	if (!UnitDef)
	{
		return 1.f;
	}

	float Rate = 1.f;
	if (Data.ScaleMode == EAsceTimelineScaleMode::AttackSpeed)
	{
		const FMassStateTreeExecutionContext& MassContext = static_cast<const FMassStateTreeExecutionContext&>(Context);
		const FAsceCombatAttributesFragment* Combat = MassContext.GetEntityManager()
			.GetFragmentDataPtr<FAsceCombatAttributesFragment>(MassContext.GetEntity());
		if (Combat && UnitDef->BaseAttackSpeed > 0.f)
		{
			Rate = Combat->AttackSpeed / UnitDef->BaseAttackSpeed;
		}
	}
	else if (Data.ScaleMode == EAsceTimelineScaleMode::CastSpeed)
	{
		Rate = 1.f + UnitDef->CastSpeedPercent / 100.f;
	}

	return FMath::Clamp(Rate, 0.05f, 20.f);
}

EStateTreeRunStatus FAsceWaitTimelineTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.bLoggedStuck = false;
	const UWorld* World = Context.GetWorld();
	const FAsceBehaviorFragment* Behavior = Context.GetExternalDataPtr(TimelineHandle);
	if (!World || !Behavior || !Behavior->IsTimelineActive())
	{
		Data.TargetTime = 0.f;
		UE_LOG(LogTemp, Verbose, TEXT("[Wait] 进入 Time=%.2f：无时间轴 ⇒ 直接通过"), Data.Time);
		return EStateTreeRunStatus::Succeeded;
	}
	const float Rate = ResolveTimeScale(Context, Data);
	Data.TargetTime = Data.Time / Rate;
	const float Elapsed = AsceTimeline::GetTime(*World, *Behavior);
	const bool bReached = Elapsed >= Data.TargetTime;
	UE_LOG(LogTemp, Verbose, TEXT("[Wait] 进入 Time=%.2f Rate=%.2f Target=%.2f Elapsed=%.2f ⇒ %s"),
		Data.Time, Rate, Data.TargetTime, Elapsed, bReached ? TEXT("已到达") : TEXT("等待中"));
	return bReached ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FAsceWaitTimelineTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
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
		UE_LOG(LogTemp, Verbose, TEXT("[Wait] 到达 Target=%.2f Elapsed=%.2f"), Data.TargetTime, Elapsed);
		return EStateTreeRunStatus::Succeeded;
	}
	if (!Data.bLoggedStuck && Elapsed >= 3.f)
	{
		Data.bLoggedStuck = true;
		UE_LOG(LogTemp, Verbose, TEXT("[Wait] 等待超 3s 仍未到达：Target=%.2f Elapsed=%.2f"), Data.TargetTime, Elapsed);
	}
	return EStateTreeRunStatus::Running;
}
