// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"

#include "AscePlayMoveAnimTask.generated.h"

struct FAsceAnimPlaybackFragment;
struct FAsceUnitDefFragment;
namespace UE::MassBehavior
{
	struct FStateTreeDependencyBuilder;
}

USTRUCT()
struct FAscePlayMoveAnimTaskInstanceData
{
	GENERATED_BODY()

	/** 播放速率倍率（1 = 按烘焙帧率） */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	float PlayRateScale = 1.f;

	/** 当前动画桶（0=受伤移动 / 1=奔跑 / 2=普通移动）；变化时重选动画 */
	int32 CurrentBucket = INDEX_NONE;
};

/**
 * **移动动画**：按当前状态选动画（循环）：
 *  - 生命值 < 15% ⇒ 受伤移动（`InjuredMoveAnims`）；
 *  - 否则当前速度 ≥ 1.7 × 基础速度 ⇒ 奔跑（`RunAnims`）；
 *  - 否则普通移动（`MoveAnims`）。
 *
 * 受伤优先级高于奔跑。基础速度 = `FAsceRTSUnitDef::BaseMoveSpeed`（最大速度 = 2×基础）。
 * 动画数据在实体定义表；帧段在 `UAsceAnimSet::Clips`；客户端写 cue。
 */
USTRUCT(meta = (DisplayName = "Asce Play Move Anim", Category = "Asce"))
struct FAscePlayMoveAnimTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAscePlayMoveAnimTaskInstanceData;

	FAscePlayMoveAnimTask();

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;

	/** 按当前速度/血量算桶，并在变化时换动画；返回 Running（一直占住 Move 状态） */
	EStateTreeRunStatus UpdateAnimation(FStateTreeExecutionContext& Context, FInstanceDataType& Data) const;

	TStateTreeExternalDataHandle<FAsceAnimPlaybackFragment, EStateTreeExternalDataRequirement::Optional> PlaybackHandle;
	TStateTreeExternalDataHandle<FAsceUnitDefFragment, EStateTreeExternalDataRequirement::Optional> UnitDefHandle;
};
