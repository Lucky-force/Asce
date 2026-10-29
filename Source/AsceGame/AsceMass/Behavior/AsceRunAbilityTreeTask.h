// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"
#include "StateTreeInstanceData.h"

#include "AsceRunAbilityTreeTask.generated.h"

class UStateTree;
struct FAsceActionDef;

USTRUCT()
struct FAsceRunAbilityTreeTaskInstanceData
{
	GENERATED_BODY()

	/** 技能子树的实例数据（按实体一份；容量跨技能复用） */
	UPROPERTY(Transient)
	FStateTreeInstanceData TreeInstanceData;

	/** 当前正在跑的技能树（Stop/Exit 用） */
	UPROPERTY(Transient)
	TObjectPtr<const UStateTree> RunningStateTree = nullptr;

	/** 诊断：子树连续运行时长（秒）/ 是否已打印卡住日志 / 是否已打印结束日志 */
	float RunningElapsedTime = 0.f;
	bool bLoggedStuck = false;
	bool bLoggedFinished = false;
};

/**
 * **Mass 感知的技能子树任务**（`Combat/技能编辑器设计.md` §3.1 的 RunAbilityTree）。
 *
 * 为什么不用引擎的 `FStateTreeRunParallelStateTreeTask`：
 *  ① Mass schema 只允许 `FMassStateTreeTaskBase` 派生（`MassStateTreeSchema.cpp:20`），通用 task 根本不在列表里；
 *  ② 它的子上下文是普通 `FStateTreeExecutionContext`（没有 entity/manager；外部数据按父上下文下标拷贝）。
 * 这里直接从 `FMassStateTreeTaskBase` 派生，并复用父上下文的 `FMassExecutionContext` + entity 构造
 * `FMassStateTreeExecutionContext`：Mass 的 `CollectExternalData` 会按**子树自己的 schema 依赖**
 * 从该 entity 重新收集 fragment ⇒ 技能子树里的现有 task 不用改。
 *
 * 选树：服务器读 `FAsceActionFragment::ActionDefId`、客户端读 `FAsceCombatVisualFragment::ActionDefId`，
 * 查动作表行的 `SkillTree`。子树由父树的 tick 内联推进（`UAsceStateTreeTickDriver` 每帧唤醒时间轴在跑的单位）。
 */
USTRUCT(meta = (DisplayName = "Asce Run Ability Tree", Category = "Asce"))
struct FAsceRunAbilityTreeTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAsceRunAbilityTreeTaskInstanceData;

	FAsceRunAbilityTreeTask();

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void TriggerTransitions(FStateTreeExecutionContext& Context) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

private:
	/** 按当前动作的 ActionDefId 取动作行（两端来源不同）；查不到/没配 SkillTree ⇒ false + 告警 */
	static bool ResolveActionDef(FStateTreeExecutionContext& Context, FAsceActionDef& OutDef);
};
