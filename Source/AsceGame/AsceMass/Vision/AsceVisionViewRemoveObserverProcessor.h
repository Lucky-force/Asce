// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassObserverProcessor.h"
#include "MassEntityQuery.h"
#include "AsceVisionViewRemoveObserverProcessor.generated.h"

/**
 * 视野单位（观察者）的 Remove Observer（架构文档 §5.2/§5.4 的死亡清理）：
 * 带 FAsceVisionViewFragment 的单位消亡（fragment 被移除 / 实体销毁）时，事件驱动地
 * 下线它残留的视野贡献——在实体销毁前该实体的数据仍完整可读，直接读它自己的
 * FAsceVisionFogStateFragment（上帧贡献状态：cell/He/R/team）按旧参数调
 * Fog::RemoveContribution。Fog 侧无需维护"单位 → 记录"的集中表。
 *
 * 不需要 Add Observer：单位创建后由 AsceFogProcessor 每帧首次 diff 时上线即可。
 * Server/Client 都执行（两端各自维护自己的贡献表：Server 过滤 / Client 表现）。
 */
UCLASS()
class UAsceVisionViewRemoveObserverProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UAsceVisionViewRemoveObserverProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
