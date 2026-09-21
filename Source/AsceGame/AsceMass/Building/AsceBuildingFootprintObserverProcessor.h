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
 * footprint 的 Add 观察者（costfield/视野/本观察者）都在**同一次结构变更**里由 Mass 的
 * observer pipeline 连续执行（`FMassObserverManager::HandleElementsImpl`），期间没有外部代码
 * 可以插入；若完工时手动提前清占位，一旦结构变更被批量/延迟 flush，占位清除与 costfield
 * 写入就会被拆开，出现"占位已清、costfield 还没写"的空窗。
 *
 * 顺序保证：本观察者与 costfield/视野观察者在 pipeline 里谁先谁后都不影响结论 —— 它们都在
 * 同一次结构变更里紧接着彼此执行 ⇒ 控制权回到游戏代码（含调度器里的放置校验）时，costfield
 * 已经写完、占位也已释放，不存在"两边都不挡"的窗口。
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
