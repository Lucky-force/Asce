// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"

#include "AsceStateTreeTickDriver.generated.h"

/**
 * **把"时间轴在跑"的实体每帧唤醒一次**（`Combat/技能编辑器设计.md` §10.3）。
 *
 * 为什么需要：Mass 的树是**信号驱动**的（只在收到信号时 tick），而时间轴节点要按绝对时间到点执行——
 * 所以每个"时间轴在跑"的实体（`FAsceBehaviorFragment::bTimelineActive`）每帧喂一条
 * `UE::Mass::Signals::NewStateTreeTaskRequired`，树里的 `Asce Wait Timeline` 之类的 task 才能按时完成。
 *
 * 只对 `bActive` 的实体发信号（正在施法的那些，数量天然小）；idle/移动单位零成本。
 */
UCLASS()
class ASCEGAME_API UAsceStateTreeTickDriver : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceStateTreeTickDriver();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;

	/** 每帧复用（避免逐帧分配）：本帧需要唤醒的实体 */
	TArray<FMassEntityHandle> ActiveEntitiesScratch;
};
