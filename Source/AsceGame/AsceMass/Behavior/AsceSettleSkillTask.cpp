// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AsceSettleSkillTask.h"

#include "Engine/World.h"
#include "MassStateTreeDependency.h"          // FStateTreeDependencyBuilder 的定义
#include "MassStateTreeExecutionContext.h"    // FMassStateTreeExecutionContext（取实体/EntityManager）
#include "MassStateTreeTypes.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

#include "AsceMass/Combat/AsceCombatFragments.h"     // FAsceActionFragment
#include "AsceMass/Combat/AsceSkillCastLibrary.h"    // AsceSkillCast::SettleFromAction
#include "AsceMass/Fragments/AsceCoreFragments.h"    // FAsceTeamFragment / AsceTeamId

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceSettleSkillTask)

FAsceSettleSkillTask::FAsceSettleSkillTask()
{
	// 只在进入状态时结算一次
	bShouldCallTick = false;
}

bool FAsceSettleSkillTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(ActionHandle);
	Linker.LinkExternalData(TeamHandle);
	return true;
}

void FAsceSettleSkillTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadOnly<FAsceActionFragment>();
	Builder.AddReadOnly<FAsceTeamFragment>();
}

EStateTreeRunStatus FAsceSettleSkillTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	UWorld* World = Context.GetWorld();
	const FAsceActionFragment* Action = Context.GetExternalDataPtr(ActionHandle);
	if (!World || !Action)
	{
		return EStateTreeRunStatus::Succeeded;   // 没在动作里：不阻塞树
	}

	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	const FAsceTeamFragment* Team = Context.GetExternalDataPtr(TeamHandle);
	const int32 TeamId = Team ? AsceTeamId(Team->TeamKey) : 0;

	// 结算（内部按端早退：客户端直接 true）
	const bool bSettled = AsceSkillCast::SettleFromAction(MassContext.GetEntityManager(), World
		, MassContext.GetEntity(), *Action, TeamId, Data.bPayCost);
	return bSettled ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}
