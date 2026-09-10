// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "AsceMassCollisionSolverProcessor.generated.h"

struct FMassEntityQuery;
struct FWorldMapData;

/**
 * Mass 版"位置级碰撞求解器"（position-based dynamics），合并了：
 *   1. entity↔entity 圆-圆接触（穿透量对半分，对称收敛）
 *   2. entity↔墙格簇 圆-AABB接触（墙不可撼动，全量推开）
 *
 * 为什么这么做（迭代自"Separation"的教训）：
 * - 墙格只有一个预计算斥力方向，但entity可能从任意方向被挤进来；
 *   每帧跟随当前格子的方向会在墙格里"之字形滑动"，出不去。
 * - 改成"墙格簇"后：邻接墙格合并成一个尽量大的轴对齐矩形（FWallCluster），
 *   对簇做一次圆-AABB，法线=圆心到簇最近点的方向——始终垂直墙面、朝簇外，
 *   不存在"朝墙内部推"的错误（逐格解算会在相邻格交界把法线算歪）。
 * - 速度投影：贴墙时去掉速度在墙面法线上的分量（只留切向）→ 单位沿墙滑动，
 *   而不是每帧被 ApplyMovement 再往墙里怼。
 *
 * 执行方式：
 * - ParallelForEachEntityChunk（并行）。SpatialGrid只读查询、墙簇只读，并行安全；
 *   邻居Transform读取容忍一帧旧值（Jacob迭代，跨chunk精度损失可接受）。
 * - 两轮迭代（每轮用上一轮写回后的位置再解一遍）——人挤人收敛快，不用多帧慢慢挤。
 *
 * 墙簇数据源（B方案）：每帧Execute开头在GameThread从 PathFindingSubsystem 取一次
 * const FWorldMapData* 缓存（墙簇TArray只读、非复制数据两端本地重建），
 * 不声明UAscePathFindingSubsystem的query需求（它内部有可变scratch是GameThreadOnly，
 * 我们只读其WorldMapData的WallClusters，不碰scratch，所以绕开）。
 */
UCLASS()
class UAsceMassCollisionSolverProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceMassCollisionSolverProcessor();

	/** 邻居查询半径（世界单位）。应 >= 最大CombinedRadius*2，取大了多查几个格子浪费，取小了漏掉穿透 */
	UPROPERTY(EditAnywhere, Category = "CollisionSolver")
	float QueryRadius = 300.f;

	/** 圆-圆推开比例（0.5=双方各推一半；调小收敛慢但更稳，调大每帧修正更多） */
	UPROPERTY(EditAnywhere, Category = "CollisionSolver")
	float EntitySeparatonRatio = 0.5f;

	/** 迭代求解轮数（人挤人集群：2轮足够；更多轮收敛更硬但每轮都重算接触） */
	UPROPERTY(EditAnywhere, Category = "CollisionSolver")
	int32 SolverIterations = 2;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
