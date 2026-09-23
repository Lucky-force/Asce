// AsceEtherFragments.h
#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "AsceEtherFragments.generated.h"

/**
 * 标记：该实体是一个 ether 建筑（注册/注销锚点）。
 *
 * - **身份 id** 统一在 `FAsceUnitDefFragment::UniqueTypeId`（= 实体定义表行名数字）；
 *   observer 在 `AddNode` 时按它换成 `FName` 行名；
 * - 端口 / 布线 / 在制品等**实例数据**都在 `UAsceEtherNetworkSubsystem`（不在 fragment）；
 * - 器件内部状态在对应的 `UAsceEtherProcessCore`（`bInstance = true` 时每个建筑一个实例）；
 * - `Revision` = "**结构/配置**上的外部数据修订号"：Ether 图拓扑、端口配置、内布线等变化时由 Ether 自增，
 *   UI 订阅它即可知道"该建筑的 Ether 结构数据变过"（**不管料流**；`Ether`/信号值逐帧在动，面板自己轮询读）。
 */
USTRUCT()
struct FEtherBuildingFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 只增（回绕无害，订阅方只判断"不等"）；以后需要分方面就在本 fragment 里加字段，粒度由 Ether 自己定 */
	UPROPERTY()
	uint8 Revision = 0;
};
