// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"

#include "AscePlayIdleAnimTask.generated.h"

struct FAsceAnimPlaybackFragment;
struct FAsceUnitDefFragment;
namespace UE::MassBehavior
{
	struct FStateTreeDependencyBuilder;
}

USTRUCT()
struct FAscePlayIdleAnimTaskInstanceData
{
	GENERATED_BODY()

	/** 播放速率倍率（1 = 按烘焙帧率） */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	float PlayRateScale = 1.f;

	/** 距离下一次 idlebreak 的剩余秒数（运行时） */
	float TimeUntilBreak = 0.f;

	/** 正在播 idlebreak */
	bool bPlayingBreak = false;

	/** idlebreak 剩余秒数（运行时；播 break 期间**不累计**间隔） */
	float BreakRemaining = 0.f;
};

/**
 * **Idle 动画**：进入状态播 idle（循环，多变异随机一个）；
 * 每隔一段**随机间隔**从 `IdleBreakAnims` 里随机一个播一次（播 break 期间不累计间隔）。
 *
 * - 动画数据在实体定义表（`IdleAnims` / `IdleBreakAnims`）；
 * - 间隔写死一个随机区间（`[MinIdleBreakInterval, MaxIdleBreakInterval]`，每轮重新摇）；
 * - 保持 `Running` 占住状态（靠事件转移离开）；专用服务器只占状态不写 cue。
 */
USTRUCT(meta = (DisplayName = "Asce Play Idle Anim", Category = "Asce"))
struct FAscePlayIdleAnimTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAscePlayIdleAnimTaskInstanceData;

	FAscePlayIdleAnimTask();

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;

	/** 随机一段 idlebreak 间隔（写死的区间） */
	static float RollNextBreakInterval();

	/** 播一次 idlebreak（返回时长秒；失败返回 0） */
	static float PlayRandomBreak(FAsceAnimPlaybackFragment& Playback, const struct FAsceRTSUnitDef& UnitDef, float PlayRateScale);

	TStateTreeExternalDataHandle<FAsceAnimPlaybackFragment, EStateTreeExternalDataRequirement::Optional> PlaybackHandle;
	TStateTreeExternalDataHandle<FAsceUnitDefFragment, EStateTreeExternalDataRequirement::Optional> UnitDefHandle;
};
