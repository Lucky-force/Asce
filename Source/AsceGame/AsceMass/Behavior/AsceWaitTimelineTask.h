// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"

#include "AsceWaitTimelineTask.generated.h"

struct FAsceBehaviorFragment;
namespace UE::MassBehavior
{
	struct FStateTreeDependencyBuilder;
}

/**
 * **等到时间轴的某一刻**（`Combat/技能编辑器设计.md` §10.3 的"节点时间门槛"）。
 *
 * 用法：把它挂在状态里，转移条件用 **`On State Completed`** ⇒ "到 0.4s 时离开本状态、进入下一段"。
 * 时间从 `FAsceBehaviorFragment`（起轴时刻 + 服务器世界时间）算，**不是**逐帧累加 ⇒ 卡顿会被自然吸收。
 * 逐帧 tick 由 `UAsceStateTreeTickDriver` 保证（它给"时间轴在跑"的实体每帧发唤醒信号）。
 *
 * 没配时间轴（`bActive==0`）时直接 `Succeeded`（当成"立即通过"，不破坏树）。
 */
USTRUCT()
struct FAsceWaitTimelineTaskInstanceData
{
	GENERATED_BODY()

	/** 等到这一刻（秒，相对起轴；0 = 立即通过） */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	float Time = 0.f;

	/** 诊断：是否已打印过长等待（进入时重置） */
	bool bLoggedStuck = false;
};

USTRUCT(meta = (DisplayName = "Asce Wait Timeline", Category = "Asce"))
struct FAsceWaitTimelineTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAsceWaitTimelineTaskInstanceData;

	FAsceWaitTimelineTask();

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;

	/** `FAsceBehaviorFragment`（外部数据：读起轴时刻） */
	TStateTreeExternalDataHandle<FAsceBehaviorFragment, EStateTreeExternalDataRequirement::Optional> TimelineHandle;
};
