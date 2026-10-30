// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AsceRunAbilityTreeTask.h"

#include "Engine/World.h"
#include "MassEntityManager.h"
#include "MassStateTreeExecutionContext.h"
#include "StateTree.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeInstanceData.h"

#include "AsceMass/Combat/AsceActionDefLibrary.h"
#include "AsceMass/Combat/AsceCombatDef.h"
#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"   // FAsceUnitDefFragment（诊断日志）

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRunAbilityTreeTask)

namespace
{
	FString JoinStateNames(const TArray<FName>& Names)
	{
		FString Result;
		for (const FName& Name : Names)
		{
			if (!Result.IsEmpty())
			{
				Result += TEXT(" > ");
			}
			Result += Name.ToString();
		}
		return Result.IsEmpty() ? TEXT("（无）") : Result;
	}
}

FAsceRunAbilityTreeTask::FAsceRunAbilityTreeTask()
{
	bShouldCallTick = true;   // 子树由本任务的 Tick 内联推进
	// ★ 必须在父树的转移阶段被调用，否则 `TriggerTransitions` 里的 `Child.TickTriggerTransitions()`
	//   永远不会执行——子树的 **On State Completed** 转移（以及 tick/event 转移）就全部不生效，
	//   表现为"子树状态完成了但状态机不往下走"（对比引擎 FStateTreeRunParallelStateTreeTask 同样置 true）
	bShouldAffectTransitions = true;
	// 子树由 UAsceStateTreeTickDriver 的信号唤醒，不需要参与引擎的 scheduled tick
	bConsideredForScheduling = false;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

bool FAsceRunAbilityTreeTask::ResolveActionDef(FStateTreeExecutionContext& Context, FAsceActionDef& OutDef)
{
	const UWorld* World = Context.GetWorld();
	const FMassStateTreeExecutionContext& Parent = static_cast<const FMassStateTreeExecutionContext&>(Context);
	const FMassEntityManager& EntityManager = Parent.GetEntityManager();
	const FMassEntityHandle Entity = Parent.GetEntity();

	// 两端来源不同：服务器读权威动作 fragment、客户端读复制下来的表现 fragment
	int32 ActionId = 0;
	if (World && World->GetNetMode() == NM_Client)
	{
		if (const FAsceCombatVisualFragment* Visual = EntityManager.GetFragmentDataPtr<FAsceCombatVisualFragment>(Entity))
		{
			ActionId = Visual->ActionDefId;
		}
	}
	else
	{
		if (const FAsceActionFragment* Action = EntityManager.GetFragmentDataPtr<FAsceActionFragment>(Entity))
		{
			ActionId = Action->ActionDefId;
		}
	}

	if (ActionId <= 0 || !FAsceActionDefLibrary::GetActionDef(ActionId, OutDef))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Ability] 取动作行失败：ActionId=%d（实体 %lld）"),
			ActionId, static_cast<int64>(Entity.AsNumber()));
		return false;
	}
	OutDef.SkillTree = FAsceActionDefLibrary::ResolveSkillTree(OutDef);
	if (!OutDef.SkillTree)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Ability] 动作 %d 没有 SkillTree（也不是可回落的攻击），跳过"), ActionId);
		return false;
	}
	if (!OutDef.SkillTree->IsReadyToRun())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Ability] 动作 %d 的 SkillTree %s 未加载（IsReadyToRun=false）"),
			ActionId, *OutDef.SkillTree->GetName());
		return false;
	}

	// 诊断：确认"哪个单位类型、哪个动作行、选中了哪棵树"（排查配置串行用；稳定后可降到 Verbose）
	{
		uint16 UnitTypeId = 0;
		if (const FAsceUnitDefFragment* UnitDef = EntityManager.GetFragmentDataPtr<FAsceUnitDefFragment>(Entity))
		{
			UnitTypeId = UnitDef->UniqueTypeId;
		}
		UE_LOG(LogTemp, Verbose, TEXT("[Ability] 单位类型 %u 实体 %lld 动作 %d → SkillTree=%s（NetMode=%d）"),
			UnitTypeId, static_cast<int64>(Entity.AsNumber()), ActionId, *OutDef.SkillTree->GetName(),
			World ? static_cast<int32>(World->GetNetMode()) : -1);
	}
	return true;
}

EStateTreeRunStatus FAsceRunAbilityTreeTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	FAsceActionDef Def;
	if (!ResolveActionDef(Context, Def))
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.RunningStateTree = Def.SkillTree;
	InstanceData.RunningElapsedTime = 0.f;
	InstanceData.bLoggedStuck = false;
	InstanceData.bLoggedFinished = false;

	// 共享事件队列：父树收到的停止/打断事件，子树也能看到
	if (FStateTreeInstanceData* OuterInstanceData = Context.GetMutableInstanceData())
	{
		InstanceData.TreeInstanceData.SetSharedEventQueue(OuterInstanceData->GetSharedMutableEventQueue());
	}

	FMassStateTreeExecutionContext& Parent = static_cast<FMassStateTreeExecutionContext&>(Context);
	FMassStateTreeExecutionContext Child(*Context.GetOwner(), *Def.SkillTree, InstanceData.TreeInstanceData
		, Parent.GetMassEntityExecutionContext());
	Child.SetEntity(Parent.GetEntity());
	return Child.Start();
}

EStateTreeRunStatus FAsceRunAbilityTreeTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.RunningStateTree)
	{
		return EStateTreeRunStatus::Failed;
	}

	FMassStateTreeExecutionContext& Parent = static_cast<FMassStateTreeExecutionContext&>(Context);
	FMassStateTreeExecutionContext Child(*Context.GetOwner(), *InstanceData.RunningStateTree
		, InstanceData.TreeInstanceData, Parent.GetMassEntityExecutionContext());
	Child.SetEntity(Parent.GetEntity());
	if (!Child.IsValid())
	{
		return EStateTreeRunStatus::Failed;
	}

	const EStateTreeRunStatus Status = Child.TickUpdateTasks(DeltaTime);
	InstanceData.RunningElapsedTime += DeltaTime;

	// 诊断：子树长期不结束 → 打印父/子活动状态（定位卡在哪个状态/哪个 cue）
	if (Status == EStateTreeRunStatus::Running)
	{
		if (!InstanceData.bLoggedStuck && InstanceData.RunningElapsedTime >= 3.f)
		{
			InstanceData.bLoggedStuck = true;
			UE_LOG(LogTemp, Verbose, TEXT("[Ability] 子树 %s 运行 %.1fs 未结束；父状态=[%s] 子状态=[%s]"),
				*InstanceData.RunningStateTree->GetName(), InstanceData.RunningElapsedTime,
				*JoinStateNames(Context.GetActiveStateNames()), *JoinStateNames(Child.GetActiveStateNames()));
		}
	}
	else if (!InstanceData.bLoggedFinished)
	{
		InstanceData.bLoggedFinished = true;
		UE_LOG(LogTemp, Verbose, TEXT("[Ability] 子树 %s 结束（%s），耗时 %.2fs；父状态=[%s]"),
			*InstanceData.RunningStateTree->GetName(),
			Status == EStateTreeRunStatus::Succeeded ? TEXT("Succeeded") : TEXT("Failed"),
			InstanceData.RunningElapsedTime, *JoinStateNames(Context.GetActiveStateNames()));
	}
	return Status;
}

void FAsceRunAbilityTreeTask::TriggerTransitions(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.RunningStateTree)
	{
		return;
	}

	FMassStateTreeExecutionContext& Parent = static_cast<FMassStateTreeExecutionContext&>(Context);
	FMassStateTreeExecutionContext Child(*Context.GetOwner(), *InstanceData.RunningStateTree
		, InstanceData.TreeInstanceData, Parent.GetMassEntityExecutionContext());
	Child.SetEntity(Parent.GetEntity());
	if (!Child.IsValid())
	{
		return;
	}

	const EStateTreeRunStatus LastTreeRunStatus = InstanceData.TreeInstanceData.GetStorage().GetExecutionState().TreeRunStatus;
	Child.TickTriggerTransitions();
	const EStateTreeRunStatus NewTreeRunStatus = InstanceData.TreeInstanceData.GetStorage().GetExecutionState().TreeRunStatus;
	if (LastTreeRunStatus != NewTreeRunStatus)
	{
		ensure(NewTreeRunStatus != EStateTreeRunStatus::Running);
		Context.FinishTask(*this, NewTreeRunStatus == EStateTreeRunStatus::Succeeded
			? EStateTreeFinishTaskType::Succeeded : EStateTreeFinishTaskType::Failed);
	}
}

void FAsceRunAbilityTreeTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.RunningStateTree)
	{
		return;
	}

	FMassStateTreeExecutionContext& Parent = static_cast<FMassStateTreeExecutionContext&>(Context);
	FMassStateTreeExecutionContext Child(*Context.GetOwner(), *InstanceData.RunningStateTree
		, InstanceData.TreeInstanceData, Parent.GetMassEntityExecutionContext());
	Child.SetEntity(Parent.GetEntity());
	if (Child.IsValid())
	{
		Child.Stop();
	}
	InstanceData.RunningStateTree = nullptr;
}
