// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StateTreeTaskBase.h"

#include "AsceStateTreeLogTask.generated.h"

/**
 * **验证/调试用**的最小 StateTree task：进入/退出时打日志。
 *
 * 两个用途（对应 `Combat/技能编辑器设计.md` 第 1、2 步的验证）：
 *  1. 空着用 ⇒ 确认"Mass StateTree 在**服务端和客户端都在跑**"（看日志里的 `NetMode`：听服=1 / 客户端=3）；
 *  2. 配 `EventToSendOnEnter` ⇒ 进入时给**自己这个实体**发一条树事件（验证"系统 → 树"的事件路径）：
 *     在树里让 A 状态 →(`RequiredEvent = 该 Tag`)→ B 状态，B 挂本 task，就能看到 A→B 的跳转。
 *
 * 返回 `Running` ⇒ 状态保持激活（适合挂在 idle 之类"一直待着"的状态里观察）。
 */
USTRUCT()
struct FAsceStateTreeLogTaskInstanceData
{
	GENERATED_BODY()

	/** 日志前缀（编辑器里可改，方便区分挂在哪个状态上） */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FString Message = TEXT("AsceLog");

	/** 进入本状态时给自己发这条事件（用于验证事件路径；不配 = 不发） */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FGameplayTag EventToSendOnEnter;
};

USTRUCT(meta = (DisplayName = "Asce Log (verify)", Category = "Asce"))
struct FAsceStateTreeLogTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAsceStateTreeLogTaskInstanceData;

	FAsceStateTreeLogTask();

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};
