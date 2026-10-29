// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"

#include "AsceCombatVisualProcessor.generated.h"

/**
 * 表现 processor（**cue 驱动，逻辑/表现分层**）：
 *  行为树的 `Asce Play Anim Cue` 在客户端把 AnimSet 绝对帧段写进 `FAsceAnimPlaybackFragment`；
 *  本 processor 只按时间推进帧号，并写 ISM per-instance custom data（2 floats：`Frame, PrevFrame`）。
 *  **不再按动作状态/CastConfig 解析动画**（旧回退已删除）。
 *
 * 顺序/对齐约束（改之前必读）：与 `UMassUpdateISMProcessor` 同匹配集/实体顺序、跑在 Representation 组内
 * `UMassVisualizationProcessor` 之后、`UMassUpdateISMProcessor` 之前；custom data 必须每帧为每个进 ISM
 * 更新的实例 append（`AddBatchedCustomData`，2 floats）。
 */
UCLASS()
class ASCEGAME_API UAsceCombatVisualProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceCombatVisualProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
