// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassObserverProcessor.h"
#include "MassEntityQuery.h"
#include "AsceVisionOcclusionObserverProcessor.generated.h"

/**
 * Server 权威侧的"遮挡实体"生命周期 Observer（架构文档 §3.2）：
 * 在带 FAsceGridFootprintFragment 的建筑实体创建/销毁时，事件驱动地写/清
 * UAsceFogSubsystem 的动态遮挡层——不做任何每帧扫描或全量重建。
 *
 * 领域事实：挡视野的只有建筑，而"建筑" = 挂 FAsceGridFootprintFragment 的实体，
 * 遮挡高度就在该 fragment 的 OcclusionHeightCm（0=不挡）。因此直接观察 footprint 的
 * Add/Remove 即可，一次拿到范围 + 高度，无需第二个 fragment 或 Any 判断。
 *
 * 建筑 footprint 互不重叠（放置阻挡保证），Remove 直接按 footprint 区域清零即可。
 * Client 端表现层将来用 BubbleHandler（复制通道）重建，不走这里。
 */

/** 建筑入场：把 footprint（含遮挡高）写入动态遮挡层。 */
UCLASS()
class UAsceVisionOcclusionAddObserverProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UAsceVisionOcclusionAddObserverProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

/** 建筑退场：清空其 footprint 区域。 */
UCLASS()
class UAsceVisionOcclusionRemoveObserverProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UAsceVisionOcclusionRemoveObserverProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
