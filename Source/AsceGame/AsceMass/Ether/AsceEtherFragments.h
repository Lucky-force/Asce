// AsceEtherFragments.h
#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "AsceMass/Ether/AsceEtherTypes.h"
#include "AsceMass/Ether/AsceEtherBuildingData.h"
#include "AsceEtherFragments.generated.h"

/**
 * 标记：该实体是一个 ether 建筑（注册/注销锚点 + 身份）。
 * 端口/缓冲/布线的运行态都在 UAsceEtherNetworkSubsystem（不是 fragment）；
 * 器件内部状态放各自的 fragment（见设计文档 §3.2）。
 */
USTRUCT()
struct FEtherBuildingFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 建筑类型 id（反查 UAsceEtherBuildingData 用；Subsystem 维护注册表）。 */
	FName BuildingDefId;
};

/**
 * 器件内部状态（时序单元；每个器件实体一份）。**只有器件**挂这个 fragment。
 * 放 fragment 的理由：器件之间互不共享 → 采样可并行、不需要 Subsystem 维护变长状态。
 * 当前是公共骨架；具体 op（Facility/Latch/Counter/Clock…）再按需细化/拆分。
 */
USTRUCT()
struct FEtherDeviceStateFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 生产/计数进度（后续 op 用）。 */
	uint16 Progress = 0;

	/** 锁存值 / 计数值（后续 op 用）。 */
	uint16 Value = 0;

	/** 状态机相位（后续 op 用）。 */
	uint8 Phase = 0;
};
