// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "AsceLocalFogVisibilityProcessor.generated.h"

/**
 * 表现层"本地可见性"：把实体从**本端画面**上撤掉，两条来源——
 *  ① **死亡隐藏**（两端通用）：`FAsceLifecycleFragment::IsDying()` ⇒ 隐藏。权威端死亡现场置位、
 *     客户端由 `Client_NotifyDeaths` 本地置位（见 `Networking/Mass复制设计.md` §8）；
 *  ② **迷雾视野**（只 standalone / 听服 host）："非本队且本队视野不可见" ⇒ 隐藏。
 *
 * 位置：**距离剔除（`UMassVisualizationLODProcessor`）之后、`UMassVisualizationProcessor` 决定 representation 之前**；
 * 把表现 LOD 压到 Off ⇒ `LODRepresentation[Off] = None` ⇒ 引擎按**和距离剔除完全一样**的路径撤掉实例
 * （可移动 ISM 是"每帧续期"，不提交更新即消失）。**必须每帧写**：距离 LOD 每帧重算，一次性写会被覆盖。
 *
 * 边界：
 *  - 只改表现用的 `FMassRepresentationLODFragment`；复制用的 `FMassReplicationLODFragment` 一律不碰；
 *  - **只降不升**：距离已经 Off 的直接跳过（也省掉后续判定）；本队/无队伍信息恒可见（由 Fog 的便捷判定保证）；
 *  - 专用服务器没有本地玩家（Execute 里早退）。
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
