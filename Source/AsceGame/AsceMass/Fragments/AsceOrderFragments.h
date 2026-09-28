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

	// 存平方值，不存原始半径——下单时由 FAsceMoveOrderExecutor 从 RPC 参数写入。
	// **注意**：当前到达判定（UAsceArrivalProcessor 路径1）用的是实体自身的
	// FAgentRadiusFragment.Radius（身体贴住目标点），不再读这个字段；这里保留作订单元数据
	// （RPC 参数/将来需要"自定义接受半径"时再用）。任何地方要写这个字段，仍要先自己把半径平方一次。
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
