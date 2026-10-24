// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"

#include "AsceSelectAnimVariantTask.generated.h"

struct FAsceAnimPlaybackFragment;
namespace UE::MassBehavior
{
	struct FStateTreeDependencyBuilder;
}

/**
 * **选一条动画变体**：进入状态时给 `FAsceAnimPlaybackFragment::VariantSeed` 写一个随机值。
 *
 * 用法：在树的 Idle（以及将来的 Move/Attack）状态里挂本 task；
 * `UAsceCombatVisualProcessor` 会用 `变体数组[Seed % Num]` 播（数组为空则用单位 def 的单条动画）。
 * 所以**加/换变体只改数据**（单位 def 的 `IdleAnimVariants`），不用改树、不用改代码。
 *
 * 返回 `Running`：状态保持（由别的转移离开）。
 */
USTRUCT(meta = (DisplayName = "Asce Select Anim Variant", Category = "Asce"))
struct FAsceSelectAnimVariantTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	FAsceSelectAnimVariantTask();

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;

	/** 直接写**已有的**表现播放 fragment（不再为"种子"单开一个 fragment） */
	TStateTreeExternalDataHandle<FAsceAnimPlaybackFragment, EStateTreeExternalDataRequirement::Optional> PlaybackHandle;
};
