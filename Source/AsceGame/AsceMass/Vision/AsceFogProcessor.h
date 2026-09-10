// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "AsceFogProcessor.generated.h"

class UAsceFogSubsystem;

/**
 * 视野/迷雾的 Mass 驱动 Processor（架构文档 §5.2/§5.5/§6.2 驱动侧）。
 *
 * 每帧（所有 NetMode；Server/Client 各自跑自己的那一份）：
 *  1. 扫描本地"视野观察者"（带 FAsceVisionViewFragment 的单位）→ 逐单位 diff 视野贡献
 *     （换格/遮挡导致的 H_e 变化都会被检测到）；
 *  2. ProcessHeightChanges：本帧动态遮挡变化（由遮挡实体 Add/Remove 事件写入 FogSubsystem）
 *     命中的缓存结果原位重算 + 对引用它的贡献做计数 diff。
 *
 * 注意：
 *  - 动态遮挡层不在这里每帧扫描重建——遮挡实体 Add/Remove 由
 *    UAsceVisionOcclusionObserverProcessor（观察 footprint）事件驱动写/清；
 *  - 单位消亡也不在这里兜——由 AsceVisionViewRemoveObserverProcessor（观察 FAsceVisionViewFragment
 *    的 Remove）在实体销毁前读其"上帧贡献状态" fragment，按旧参数调 Fog.RemoveContribution 下线。
 *
 * entity 数据依赖：单位需要挂 FAsceVisionViewFragment、FAsceTeamFragment、FTransformFragment、
 * FAsceVisionFogStateFragment（本地上帧贡献状态，处理器负责 diff 与更新，不复制）。
 *
 * 驱动方式：不进入 Mass 全局调度；由 UAsceFogSubsystem（UTickableWorldSubsystem）在
 * OnWorldBeginPlay 创建并 CallInitialize，Tick 里手动 UE::Mass::Executor::Run（与
 * UAsceSpatialGridSubsystem/AsceGridEntitiesUpdateProcessor 相同的模式）。
 */
UCLASS()
class UAsceFogProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceFogProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/** 视野观察者（单位）扫描：diff 视野贡献。 */
	FMassEntityQuery UnitQuery;
};
