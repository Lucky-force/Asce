// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "AsceMovementSeparationProcessor.generated.h"

struct FMassEntityQuery;

/**
 * 位置级"碰撞"修正（简称Separation）：类似物理求解器的重叠修正（penetration resolution）。
 * 独立于移动指令/寻路——不管单位有没有订单、走不走，每一帧都检查与邻居的重叠并推开，
 * 推开的是单位位置（FTransformFragment），不是速度：静止单位被推开后依然静止，不会"漂移"。
 *
 * 为什么要单独处理器而不是塞在Steering里：
 * - Steering是"主动意图"（想往哪走），Separation是"被动响应"（不允许穿模），生命周期不同；
 * - Steering只对bHasOrder的entity产生活动，Separation对全部单位生效；
 * - 放在Steering里的版本是velocity层面软排斥，取消订单后就失效，且单位会带着排斥力滑走。
 *
 * 实现细节：
 * - 只处理重叠（Dist < CombinedRadius），推开量为穿透深度一半，双方各自推自己一半——
 *   两者都处理同一对重叠的两半，合计正好一个完整穿透量，收敛且对称；
 * - 查询半径取一个超过最大CombinedRadius*2的量（默认300，配置后在CDO里可调）；
 * - 每帧多对重叠时一次Pass可能推不干净（A推出又撞B），这里不做迭代求解——
 *   下一帧会继续修正，几帧内自然收敛，不做N次迭代保证帧率。
 *
 * 注意：本Processor的查询仍用串行ForEachEntityChunk（SpatialGrid的FindEntities是只读的，
 * 但循环内会读邻居的FTransformFragment，串行读取才保证看到的是这一帧已更新版本；
 * 后续如果要并行，需要把查询视图改成一次拿全数组）。
 */
UCLASS()
class UAsceMovementSeparationProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceMovementSeparationProcessor();

	/** 邻居查询半径（世界单位）。应 >= 最大单位CombinedRadius*2，取大了多查几个格子浪费，取小了漏掉穿透 */
	UPROPERTY(EditAnywhere, Category = "Separation")
	float QueryRadius = 300.f;

	/** 单次修正的推开比例（0.5=双方各推一半；调小时收敛变慢但更稳，调大时每帧修正得更多） */
	UPROPERTY(EditAnywhere, Category = "Separation")
	float SeparatonRatio = 0.5f;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
