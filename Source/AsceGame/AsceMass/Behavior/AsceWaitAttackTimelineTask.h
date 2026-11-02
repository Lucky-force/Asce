// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"

#include "AsceWaitAttackTimelineTask.generated.h"

struct FAsceBehaviorFragment;
namespace UE::MassBehavior
{
	struct FStateTreeDependencyBuilder;
}

/**
 * **攻击专用时间轴等待**：基础时长取单位 def 的 `BaseAttackTime`，再按攻速缩放。
 *
 * - 目标时刻 = `进入本节点的时间轴时刻 + BaseAttackTime × TimeRatio / (AttackSpeed / BaseAttackSpeed)`；
 * - `TimeRatio`：本段占基础攻击时间的比例，默认 1（整段）；前摇/后摇可以分成两个节点（如 0.7 / 0.3）；
 * - 读不到单位 def 时退回 `FallbackTime`；`bApplyAttackSpeed=false` 时不做攻速缩放；
 * - 与 `Asce Wait Timeline` 一样，转移条件用 `On State Completed`，时间轴由 `AsceBehaviorFragment` 锚定。
 */
USTRUCT()
struct FAsceWaitAttackTimelineTaskInstanceData
{
	GENERATED_BODY()

	/** 本段占 BaseAttackTime 的比例（从进入本节点起算；前摇/后摇如 0.7 / 0.3） */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.01", ClampMax = "10.0"))
	float TimeRatio = 1.f;

	/** 拿不到单位 def 时的兜底时长（秒） */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.01"))
	float FallbackTime = 1.f;

	/** 是否按 AttackSpeed / BaseAttackSpeed 缩放 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bApplyAttackSpeed = true;

	/** 运行时：缩放后真正要等到的绝对时间（进入时算） */
	float TargetTime = 0.f;

	/** 诊断：是否已打印过长等待（进入时重置） */
	bool bLoggedStuck = false;
};

USTRUCT(meta = (DisplayName = "Asce Wait Attack Timeline", Category = "Asce"))
struct FAsceWaitAttackTimelineTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAsceWaitAttackTimelineTaskInstanceData;

	FAsceWaitAttackTimelineTask();

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;

	/** `FAsceBehaviorFragment`（外部数据：读起轴时刻） */
	TStateTreeExternalDataHandle<FAsceBehaviorFragment, EStateTreeExternalDataRequirement::Optional> TimelineHandle;
};
