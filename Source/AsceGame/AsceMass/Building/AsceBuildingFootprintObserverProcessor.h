// AsceBuildingFootprintObserverProcessor.h
#pragma once

#include "MassObserverProcessor.h"
#include "MassEntityQuery.h"
#include "AsceBuildingFootprintObserverProcessor.generated.h"

/**
 * 建筑系统侧的 `FAsceGridFootprintFragment` **Add** 观察者：footprint 真正挂上之后，
 * 通知 `UAsceBuildingSubsystem` 释放对应的"建造中占位"。
 *
 * 为什么由观察者来做这件事（而不是完工那一刻自己清）：
 * 完工时（`CompleteConstruction`）只是把 footprint 加上，**costfield 要等下一个 Mass 阶段**
 * 才由 `AsceCostFieldFootprintObserverProcessor` 写阻挡 ⇒ 若完工时就清占位，会出现
 * "占位已清、costfield 还没写"的空窗，同帧后续的放置校验会钻过去。
 *
 * 顺序保证：本观察者与 costfield/视野观察者是同一 Mass 阶段（PrePhysics）里的两个处理器，
 * 而**所有** Mass 阶段都跑在我们的调度器 Tick 之前 ⇒ 等到调度器里的放置校验执行时，
 * costfield 已经写完、占位也已释放，不存在"两边都不挡"的窗口。
 */
UCLASS()
class UAsceBuildingFootprintObserverProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UAsceBuildingFootprintObserverProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
