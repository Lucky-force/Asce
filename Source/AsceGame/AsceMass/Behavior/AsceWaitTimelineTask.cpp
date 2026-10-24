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
	Builder.AddReadWrite<FAsceBehaviorFragment>();
}

EStateTreeRunStatus FAsceWaitTimelineTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	const UWorld* World = Context.GetWorld();
	const FAsceBehaviorFragment* Behavior = Context.GetExternalDataPtr(TimelineHandle);
	if (!World || !Behavior || !Behavior->IsTimelineActive())
	{
		// 没在跑时间轴：直接通过（不破坏树）
		return EStateTreeRunStatus::Succeeded;
	}
	return AsceTimeline::HasReached(*World, *Behavior, Data.Time)
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FAsceWaitTimelineTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	const UWorld* World = Context.GetWorld();
	const FAsceBehaviorFragment* Behavior = Context.GetExternalDataPtr(TimelineHandle);
	if (!World || !Behavior || !Behavior->IsTimelineActive())
	{
		return EStateTreeRunStatus::Succeeded;
	}
	return AsceTimeline::HasReached(*World, *Behavior, Data.Time)
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Running;
}
