// AsceEtherFragments.h
#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "AsceEtherFragments.generated.h"

/**
 * 标记：该实体是一个 ether 建筑（注册/注销锚点 + 身份）。
 *
 * - 端口 / 布线 / 在制品等**实例数据**都在 `UAsceEtherNetworkSubsystem`（不是 fragment）；
 * - 器件内部状态在对应的 `UAsceEtherProcessCore`（`bInstance = true` 时每个建筑一个实例）。
 */
USTRUCT()
struct FEtherBuildingFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 建筑配置 id（反查 `TSubclassOf<UAsceEtherBuildingConfig>`；Subsystem 维护注册表）。 */
	FName BuildingDefId;
};
