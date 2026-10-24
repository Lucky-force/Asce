// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AsceStateTreeLogTask.h"

#include "AsceMass/Behavior/AsceStateTreeEvents.h"
#include "Engine/World.h"
#include "MassStateTreeExecutionContext.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceStateTreeLogTask)

FAsceStateTreeLogTask::FAsceStateTreeLogTask()
{
	// 只关心进入/退出（不需要每帧 tick）
	bShouldCallTick = false;
}

EStateTreeRunStatus FAsceStateTreeLogTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	const UWorld* World = Context.GetWorld();
	UE_LOG(LogTemp, Log, TEXT("[StateTree] Enter '%s'  NetMode=%d  Owner=%s"),
		*Data.Message,
		World ? static_cast<int32>(World->GetNetMode()) : -1,
		*Context.GetOwner()->GetName());

	if (Data.EventToSendOnEnter.IsValid())
	{
		// Mass 下执行上下文一定是 `FMassStateTreeExecutionContext`（引擎任务也是这么取 entity 的）
		const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
		if (UWorld* MutableWorld = Context.GetWorld())
		{
			const bool bSent = AsceStateTree::SendEventToEntity(*MutableWorld, MassContext.GetEntityManager()
				, MassContext.GetEntity(), Data.EventToSendOnEnter, FConstStructView(), TEXT("AsceLogTask"));
			UE_LOG(LogTemp, Log, TEXT("[StateTree]   发送事件 %s ⇒ %s"), *Data.EventToSendOnEnter.ToString()
				, bSent ? TEXT("已入队+唤醒") : TEXT("失败（没有树实例？）"));
		}
	}

	return EStateTreeRunStatus::Running;
}

void FAsceStateTreeLogTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	const UWorld* World = Context.GetWorld();
	UE_LOG(LogTemp, Log, TEXT("[StateTree] Exit  '%s'  NetMode=%d"),
		*Data.Message,
		World ? static_cast<int32>(World->GetNetMode()) : -1);
}
