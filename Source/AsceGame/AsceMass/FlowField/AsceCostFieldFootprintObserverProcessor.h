// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassObserverProcessor.h"
#include "MassEntityQuery.h"
#include "AsceCostFieldFootprintObserverProcessor.generated.h"

/**
 * 寻路侧的"建筑 footprint 生命周期" Observer：
 * 在带 FAsceGridFootprintFragment 的建筑实体创建/销毁时，事件驱动地把建筑占的格子
 * 写入/回滚 CostField 阻挡（建筑不可通行）。
 *
 * - footprint 的原生坐标就是 flowfield 格（OriginGridLocation/Width/Height），零换算；
 * - 建筑 footprint 互不重叠，且放置校验保证落在"可通行的空地"上 → Add 全格写 block(255)，
 *   Remove 全格恢复为可通行(1)，不需要引用计数 / 记录原值；
 *   （若未来引入地形 cost 加权，放置校验要求原格 cost==1 的约束随之生效即可。）
 * - 观察同一 fragment 的还有 Vision/AsceVisionOcclusionObserverProcessor（写视野遮挡），
 *   两模块各自独立订阅同一事件源，互不依赖。
 *
 * 改动记录：本文件由 AsceFootprintFragment.h 旧 TODO（Add/Remove 写 CostField）落地而来；
 * 旧 TODO 里"运行时寻路 Subsystem 还没搭"的前提现已满足（UAscePathFindingSubsystem +
 * FWorldMapData + SetCostField 管线已在 FlowField 模块实现）。
 */

/** 建筑入场：把 footprint 覆盖格写成阻挡。 */
UCLASS()
class UAsceCostFieldFootprintAddObserverProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UAsceCostFieldFootprintAddObserverProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

/** 建筑退场：把 footprint 覆盖格恢复为可通行。 */
UCLASS()
class UAsceCostFieldFootprintRemoveObserverProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UAsceCostFieldFootprintRemoveObserverProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
