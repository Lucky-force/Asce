// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassObserverProcessor.h"
#include "MassEntityQuery.h"
#include "AsceCostFieldFootprintObserverProcessor.generated.h"

/**
 * 寻路侧的"建筑 footprint 生命周期" Observer：
 * 在带 FAsceGridFootprintFragment 的建筑实体创建/销毁时，事件驱动地把建筑占的格子写进/撤销
 * "建筑代价层"（FAsceLayeredCostField::BuildingCost），并逐格把"层组合结果"写回 CostFields
 * （只在真变时写 ⇒ 才触发复制 + 扇区/墙簇重建）。
 *
 * - footprint 的原生坐标就是 flowfield 格（OriginGridLocation/Width/Height），零换算；
 * - 建筑 footprint 互不重叠 ⇒ 建筑层是单值（0 = 无建筑 / BlockedCost = 阻挡），不做引用计数；
 * - Remove 不再硬写"可通行(1)"，而是清掉建筑层、由 BaseLand 基线恢复该格原值 ⇒ 修掉了
 *   "建筑压在带地形代价/墙的格上时，拆除会把地形一并抹成可通行"的问题；
 * - 观察同一 fragment 的还有 Vision/AsceVisionOcclusionObserverProcessor（写视野遮挡），
 *   两模块各自独立订阅同一事件源，互不依赖。
 *
 * 改动记录：本文件由 AsceFootprintFragment.h 旧 TODO（Add/Remove 写 CostField）落地而来；
 * 旧 TODO 里"运行时寻路 Subsystem 还没搭"的前提现已满足（UAscePathFindingSubsystem +
 * FWorldMapData + SetCostField 管线已在 FlowField 模块实现）。
 * 后续改造：写入统一走 FAsceLayeredCostField（BaseLand/Building/Special 分层），不再直接写 CostFields。
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
