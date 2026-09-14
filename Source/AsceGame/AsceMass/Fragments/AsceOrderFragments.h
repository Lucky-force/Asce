// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "MassEntityTypes.h"
#include "MassCommonTypes.h"
#include "AsceMass/FlowField/AscePathFinder.h"
#include "AsceOrderFragments.generated.h"

/**
 * 玩家/AI下发给单位的"临时意图"数据。
 */

// ──────────────────────────────────────────
// 移动命令
// ──────────────────────────────────────────

USTRUCT(BlueprintType)
struct FAsceMoveOrderFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	FVector TargetLocation = FVector::ZeroVector;

	// 存平方值，不存原始半径——到达判定(AsceMovementSteeringProcessor)每帧都在跟这个值比较，
	// 存成平方能省掉判定那里FMath::Square的重复运算；任何地方要写这个字段，都要先自己
	// 把半径平方一次再赋值（50.f半径 -> 默认值2500.f）。
	UPROPERTY(BlueprintReadWrite)
	float AcceptanceRadiusSquared = 2500.f;

	UPROPERTY(BlueprintReadWrite)
	bool bHasOrder = false;

	// 由UAsceOrderChannelComponent::ServerRequestMoveOrder下单时递增写入，配合ClientRejectOrder
	// 回传"具体是哪一次下单被拒绝"，避免玩家连续下达多个命令时，客户端收到拒绝回执却不知道对应哪一次。
	UPROPERTY(BlueprintReadOnly)
	int32 OrderSequence = 0;

	// 寻路方案句柄。生命周期（见 UAsceArrivalProcessor）：
	// - 下单时由 FAsceMoveOrderExecutor 写入并在 PathFinder 上 Acquire；
	// - 到达（自己到点 / 接触到同组已到达单位）时 Release，但字段本身**保留 RawID 不清零**
	//   （disruption 组重置、被顶飞回退寻路都要靠它找 plan）；
	// - 订单被拒绝/取消时才清零（那种情况不再是该方案组员）。
	// 不是USTRUCT，没有UPROPERTY反射——纯粹是运行时状态（一个uint32），不需要被GC追踪。
	FMergedPathPlanHandle PathPlanHandle;
};


// ──────────────────────────────────────────
// 攻击命令
// 注意：目标用FMassNetworkID寻址，不用EntityHandle/裸Index——
// 见架构文档4.1节，EntityHandle是每个MassEntityManager实例私有的编号，
// 客户端和服务器是两个独立实例，不能跨端直接传EntityHandle。
// ──────────────────────────────────────────

USTRUCT(BlueprintType)
struct FAsceAttackOrderFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY()
	FMassNetworkID TargetNetID;

	UPROPERTY(BlueprintReadWrite)
	bool bHasOrder = false;

	// 语义同FAsceMoveOrderFragment::OrderSequence
	UPROPERTY(BlueprintReadOnly)
	int32 OrderSequence = 0;

	UPROPERTY(BlueprintReadWrite)
	float AttackRange = 200.f;

	UPROPERTY(BlueprintReadWrite)
	float AttackCooldown = 1.f;

	UPROPERTY(BlueprintReadWrite)
	float AttackDamage = 25.f;

	UPROPERTY(BlueprintReadWrite)
	float TimeSinceLastAttack = 0.f;
};
