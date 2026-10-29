// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AsceWaitTimelineTask.h"

#include "Engine/World.h"
#include "MassStateTreeDependency.h"   // FStateTreeDependencyBuilder 的定义
#include "MassStateTreeTypes.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

#include "AsceMass/Behavior/AsceBehaviorFragment.h"
#include "AsceMass/Behavior/AsceTimelineLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceWaitTimelineTask)

FAsceWaitTimelineTask::FAsceWaitTimelineTask()
{
	bShouldCallTick = true;
}

bool FAsceWaitTimelineTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(TimelineHandle);
	return true;
}

void FAsceWaitTimelineTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	// 只读（时间轴标量由 Notify processor 写）
	Builder.AddReadOnly(TimelineHandle);
}

EStateTreeRunStatus FAsceWaitTimelineTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.bLoggedStuck = false;
	const UWorld* World = Context.GetWorld();
	const FAsceBehaviorFragment* Behavior = Context.GetExternalDataPtr(TimelineHandle);
	if (!World || !Behavior || !Behavior->IsTimelineActive())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[Wait] 进入 Time=%.2f：无时间轴 ⇒ 直接通过"), Data.Time);
		return EStateTreeRunStatus::Succeeded;
	}
	const float Elapsed = AsceTimeline::GetTime(*World, *Behavior);
	const bool bReached = Elapsed >= Data.Time;
	UE_LOG(LogTemp, Verbose, TEXT("[Wait] 进入 Time=%.2f Elapsed=%.2f ⇒ %s"),
		Data.Time, Elapsed, bReached ? TEXT("已到达") : TEXT("等待中"));
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
	if (Elapsed >= Data.Time)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[Wait] 到达 Time=%.2f Elapsed=%.2f"), Data.Time, Elapsed);
		return EStateTreeRunStatus::Succeeded;
	}
	if (!Data.bLoggedStuck && Elapsed >= 3.f)
	{
		Data.bLoggedStuck = true;
		UE_LOG(LogTemp, Verbose, TEXT("[Wait] 等待超 3s 仍未到达：Time=%.2f Elapsed=%.2f"), Data.Time, Elapsed);
	}
	return EStateTreeRunStatus::Running;
}
