// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"

#include "AsceStateTreeNotifyProcessor.generated.h"

/**
 * **B1：把"外层动作态变化"翻译成行为树事件**（见 `Combat/技能编辑器设计.md` §4.1）。
 *
 * 两端各跑一份，来源不同：
 * - **服务器**读权威的 `FAsceActionFragment`；
 * - **客户端**读复制下来的 `FAsceCombatVisualFragment`。
 *
 * 用 `FAsceBehaviorFragment` 做**边沿检测**（归一状态 + `Counter`）：变了才发事件
 * （`Asce.Action.Idle/Move/Attack/Cast`），并顺带把树唤醒。所以**不需要**在 6 个状态变化点各插一行。
 *
 * 执行顺序：`ExecuteBefore(Behavior)` ⇒ 本帧发的事件本帧就能被树的 tick 处理。
 */
UCLASS()
class ASCEGAME_API UAsceStateTreeNotifyProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceStateTreeNotifyProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
