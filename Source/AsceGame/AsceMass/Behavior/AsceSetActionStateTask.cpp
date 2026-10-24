// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AsceSetActionStateTask.h"

#include "Engine/World.h"
#include "MassStateTreeDependency.h"          // FStateTreeDependencyBuilder 的定义
#include "MassStateTreeTypes.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

#include "AsceMass/Combat/AsceCombatFragments.h"   // FAsceActionFragment

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceSetActionStateTask)

FAsceSetActionStateTask::FAsceSetActionStateTask()
{
	bShouldCallTick = false;
}

bool FAsceSetActionStateTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(ActionHandle);
	return true;
}

void FAsceSetActionStateTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadWrite<FAsceActionFragment>();
}

EStateTreeRunStatus FAsceSetActionStateTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	UWorld* World = Context.GetWorld();

	// 动作态是**复制的唯一真相**，只在服务器写（客户端从复制拿）
	if (!World || World->GetNetMode() == NM_Client)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	FAsceActionFragment* Action = Context.GetExternalDataPtr(ActionHandle);
	if (!Action)
	{
		return EStateTreeRunStatus::Failed;
	}

	Action->ActionState = Data.State;
	if (Data.State == EAsceActionState::Idle)
	{
		Action->ActionTarget.Reset();   // 回 Idle 才清目标（Channel 还要继续打）
	}
	if (Data.bBumpCounter)
	{
		++Action->Counter;   // 客户端据此"播一次"
	}
	return EStateTreeRunStatus::Succeeded;
}
