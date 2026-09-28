// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "AsceLocalFogVisibilityProcessor.generated.h"

/**
 * 表现层"本地迷雾可见性"：只在本端有画面、且本地世界拥有全部实体的端跑（standalone / 听服 host）。
 *
 * 位置：**距离剔除（`UMassVisualizationLODProcessor`）之后、`UMassVisualizationProcessor` 决定 representation 之前**；
 * 把"非本队且本队视野不可见"的实体表现 LOD 压到 Off ⇒ `LODRepresentation[Off] = None` ⇒ 引擎按**和距离剔除完全一样**
 * 的路径撤掉实例（可移动 ISM 是"每帧续期"，不提交更新即消失；视野回来时距离 LOD 自然恢复、实例加回）。
 *
 * 边界：
 *  - 只改表现用的 `FMassRepresentationLODFragment`；复制用的 `FMassReplicationLODFragment` 一律不碰；
 *  - 客户端不需要（敌人实体已被复制裁剪，本地压根没有）；专用服务器没有本地玩家（Execute 里早退）；
 *  - **只降不升**：距离已经 Off 的直接跳过（也省掉迷雾查询）；本队/无队伍信息恒可见（由 Fog 的便捷判定保证）。
 */
UCLASS()
class UAsceLocalFogVisibilityProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceLocalFogVisibilityProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
