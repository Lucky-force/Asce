// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"

#include "AscePlayAnimCueTask.generated.h"

class UAnimSequence;
struct FAsceAnimPlaybackFragment;
struct FAsceUnitDefFragment;
namespace UE::MassBehavior
{
	struct FStateTreeDependencyBuilder;
}

/**
 * **表现 cue：播哪条动画**（`Combat/技能编辑器设计.md` §10 的 cue 层）。
 *
 * 逻辑/表现分层：动作状态/时间轴/结算只在服务器与树的逻辑节点里；**动画完全由本任务在客户端决定**。
 * 用法：在单位树的每个状态（Idle/Move/Attack）与技能子树的时间轴节点上挂本任务，填 `Variants`
 * （进入状态时**随机选一条**）；进入状态时写 cue，表现 processor 消费 cue（帧段 → 每帧 custom data）。
 *
 * 服务器（含专用服务器）直接 `Running`，不写 cue；`bLoop`/`PlayRate` 由 cue 决定，不再从动作状态推导。
 */
USTRUCT()
struct FAscePlayAnimCueTaskInstanceData
{
	GENERATED_BODY()

	/** 候选动画（进入状态时**随机选一条**；空 = 该状态不播） */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	TArray<TSoftObjectPtr<UAnimSequence>> Variants;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bLoop = true;

	/** true = 本 cue 让状态**保持 Running**（用于 Idle/Move/Attack 这种"进状态就一直待着"的节点，
	 *  防止状态瞬时完成 → 被 On State Completed 转移回自己 → cue 每帧重播/卡第一帧）。
	 *  false（默认）= 瞬时完成，不阻塞同一状态里的 Wait/Settle 链（技能子树用）。 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bHoldState = false;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	float PlayRate = 1.f;
};

USTRUCT(meta = (DisplayName = "Asce Play Anim Cue", Category = "Asce"))
struct FAscePlayAnimCueTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAscePlayAnimCueTaskInstanceData;

	FAscePlayAnimCueTask();

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;

	/** 表现播放 fragment（**ReadWrite**：写 cue；客户端本地） */
	TStateTreeExternalDataHandle<FAsceAnimPlaybackFragment, EStateTreeExternalDataRequirement::Optional> PlaybackHandle;

	/** 单位类型（`UniqueTypeId`）→ 查单位 def 里的 AnimSet，把动画资产解析成帧段（fragment 只存 POD） */
	TStateTreeExternalDataHandle<FAsceUnitDefFragment, EStateTreeExternalDataRequirement::Optional> UnitDefHandle;
};
