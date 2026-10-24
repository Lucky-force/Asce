// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"

#include "AsceSettleSkillTask.generated.h"

struct FAsceActionFragment;
struct FAsceTeamFragment;
namespace UE::MassBehavior
{
	struct FStateTreeDependencyBuilder;
}

/**
 * **结算节点**（`Combat/技能编辑器设计.md` §10 里"节点"的落点）：对**实体当前动作**结算一次。
 *
 * - `bPayCost = true`：Trigger 节点 —— 先扣蓝 + 写槽位 CD，再结算 effect；
 * - `bPayCost = false`：持续施法的**周期节点** —— 只结算 effect（费用已在 Trigger 付过）。
 *
 * 结算实现是 `AsceSkillCast::SettleFromAction`（与 `AsceCombatProcessor` **共用同一份**）。
 * ★ **只在服务器生效**：客户端直接 `Succeeded`，让表现侧的树照常往下走。
 *
 * 典型串法：`Asce Wait Timeline (0.4)` →(On State Completed)→ 本 task →(On State Completed)→ 下一段。
 */
USTRUCT()
struct FAsceSettleSkillTaskInstanceData
{
	GENERATED_BODY()

	/** true = Trigger 节点（付费）；false = 周期节点（只结算） */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bPayCost = true;
};

USTRUCT(meta = (DisplayName = "Asce Settle Skill", Category = "Asce"))
struct FAsceSettleSkillTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAsceSettleSkillTaskInstanceData;

	FAsceSettleSkillTask();

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;

	/** 结算要用"实体当前动作"（技能 id / 目标 / 目标点 / 朝向） */
	TStateTreeExternalDataHandle<FAsceActionFragment, EStateTreeExternalDataRequirement::Optional> ActionHandle;
	/** 生成物沿用施法者阵营 */
	TStateTreeExternalDataHandle<FAsceTeamFragment, EStateTreeExternalDataRequirement::Optional> TeamHandle;
};
