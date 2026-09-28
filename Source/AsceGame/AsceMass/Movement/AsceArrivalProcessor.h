// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "AsceArrivalProcessor.generated.h"

struct FMassEntityQuery;
struct FWorldMapData;

/**
 * 到达判定处理器（纯几何，无任何时间量；权威端/客户端都跑同一套规则）。
 *
 * 两条到达路径，任一满足即算到达：
 *   路径1（无条件）：距目标 ≤ **实体自身半径**（中心进半径圈 = 身体贴住目标点）→ 到达。
 *                    与组里有没有别人到达完全无关，永远对所有未到达单位生效。
 *   路径2（接触传播）：距目标 ≤ plan 的 ArrivalRadiusSq，且与"同 Handle 的已到达单位"接触
 *                    （先圆重叠 → 同 plan → 源不比我们远 → 最后才做隔墙检测）→ 到达。
 *
 * 到达动作：bArrived=true、bHasOrder=false、ReleasePathPlan（**保留 Handle RawID**，
 * disruption 组重置 / 顶飞回退还要用），并把 plan 的 ArrivalRadiusSq 更新为
 * max(旧值, 自己到目标距离 + 自己直径 + 余量)——所以"判定圈"随到达人数按接触壳层向外生长。
 *
 * 已到达单位每帧只做一件事：自己距目标超过 plan 的 ArrivalRadiusSq → 判定被顶飞/离位 →
 * 回退寻路（bArrived=false、bHasOrder=true、重新 Acquire；plan 已结束则放弃，原地待命）。
 *
 * Disruption 重置（传送/死亡等，见 UAscePathFindingSubsystem::EnqueuePlanGroupReset）：
 * 本处理器在帧开头 drain 队列，按 plan 的成员名单**直接遍历该组**（不扫世界）恢复订单状态；
 * 之后的正常判定会让还在点附近的人当场重新成为种子，其余人重新挤、重新争抢空位。
 */
UCLASS()
class UAsceArrivalProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceArrivalProcessor();

	/** 接触判定容差（cm）：碰撞解算完仍可能留的小缝 */
	UPROPERTY(EditAnywhere, Category = "Arrival")
	float ContactToleranceCm = 5.f;

	/** 到达判定半径更新时的额外余量（cm） */
	UPROPERTY(EditAnywhere, Category = "Arrival")
	float ArrivalRadiusMarginCm = 5.f;

	/** 隔墙检测采样步长（cm）：应 ≤ 流场格边长的一半，保证不漏格 */
	UPROPERTY(EditAnywhere, Category = "Arrival")
	float WallCheckSampleStepCm = 40.f;

	/** 源空间哈希的格子边长（cm）：应 ≥ 最大接触距离（两倍最大半径 + 容差） */
	UPROPERTY(EditAnywhere, Category = "Arrival")
	float ContactHashCellCm = 200.f;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	/** 两点之间是否隔着墙（沿线段按采样步长查流场格 IsWall；不合端点本身） */
	bool IsWallBetween(const FWorldMapData& WorldMapData, const FVector2D& From, const FVector2D& To) const;

	FMassEntityQuery EntityQuery;
};
