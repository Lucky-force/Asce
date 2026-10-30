// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"

#include "AscePlayAttackAnimTask.generated.h"

struct FAsceAnimPlaybackFragment;
struct FAsceUnitDefFragment;
namespace UE::MassBehavior
{
	struct FStateTreeDependencyBuilder;
}

USTRUCT()
struct FAscePlayAttackAnimTaskInstanceData
{
	GENERATED_BODY()

	/** 播放速率倍率（1 = 按烘焙帧率；实际速率 = 攻速公式 × 这个值） */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	float PlayRateScale = 1.f;
};

/**
 * **攻击动画**：进入状态时，从实体定义表的 `AttackAnims` 里**随机选一个**播一次（不循环）。
 *
 * - 动画数据在 def（可视化编辑器 → Combat 模块 → 攻击动画数组）；
 * - 帧段在 `UAsceAnimSet::Clips`；
 * - 播放速率跟攻速走：`AttackSpeed / 200 + 0.1`（和旧表现层同一公式）；
 * - 客户端写 cue，专用服务器直接跳过。
 */
USTRUCT(meta = (DisplayName = "Asce Play Attack Anim", Category = "Asce"))
struct FAscePlayAttackAnimTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAscePlayAttackAnimTaskInstanceData;

	FAscePlayAttackAnimTask();

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;

	TStateTreeExternalDataHandle<FAsceAnimPlaybackFragment, EStateTreeExternalDataRequirement::Optional> PlaybackHandle;
	TStateTreeExternalDataHandle<FAsceUnitDefFragment, EStateTreeExternalDataRequirement::Optional> UnitDefHandle;
};
