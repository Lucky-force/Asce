// AsceEtherTypes.h
#pragma once

#include "CoreMinimal.h"
#include "AsceEtherTypes.generated.h"

/**
 * Ether（建筑网络的"电信号/物质"）通用定义。
 * 说明见 AsceMass/Ether/Ether系统设计.md：
 *  - Ether 不凭空产生（无输入 ⇒ 不输出），按 (格, 方向, 维度) 的"端口"传输；
 *  - 端口类型决定每 tick 吞吐；同一 (格,方向,维度) 只允许一条线（一族）。
 */
UENUM(BlueprintType)
enum class EAsceEtherPortOp : uint8
{
	Wire = 0 UMETA(DisplayName = "Wire"),      // 直通/路由
	And  = 1 UMETA(DisplayName = "And"),
	Or   = 2 UMETA(DisplayName = "Or"),
	Not  = 3 UMETA(DisplayName = "Not")        // 互补态翻转
};

/** 分流策略（该端口作为分流源时用；§2.9）。 */
UENUM(BlueprintType)
enum class EAsceEtherSplitMode : uint8
{
	Ordered  = 0 UMETA(DisplayName = "Ordered"),   // 灌满一个再下一个
	Balanced = 1 UMETA(DisplayName = "Balanced")   // 平均器
};

/** 器件 op（时序单元的类型；传输器不用）。 */
UENUM(BlueprintType)
enum class EAsceEtherDeviceOp : uint8
{
	Facility = 0 UMETA(DisplayName = "Facility"),  // 配方设施：扣 RecipeInputs → 产 RecipeOutput
	Sink     = 1 UMETA(DisplayName = "Sink"),      // 汇：吃掉输入
	Latch    = 2 UMETA(DisplayName = "Latch"),     // 锁存（待实现）
	Counter  = 3 UMETA(DisplayName = "Counter"),   // 计数（待实现）
	Clock    = 4 UMETA(DisplayName = "Clock")      // 自振荡（待实现）
};

namespace AsceEther
{
	/** 每个建筑支持的维度上限（端口维度范围 [0, MaxDims)）。 */
	constexpr int32 MaxDims = 8;

	/** 连线射线的最大搜索格数（防御；>= 地图尺寸即可）。 */
	constexpr int32 MaxLinkRangeCells = 256;

	/** 方向：从"左"起顺时针。 */
	enum class EDir : uint8
	{
		Left = 0,
		Up = 1,
		Right = 2,
		Down = 3,
		Count = 4
	};

	/** 建筑在 ether 网络中的角色：器件（时序）由 bIsDevice + DeviceOp 描述；传输器 = 纯组合。 */

	/** 通用 ether 类型（电力/能源）。 */
	constexpr uint16 GenericSignalType = 1;

	// 注意：port 的"唯一标识"是**全局 PortId**（= 建筑 PortStart + 建筑内 index），不是 (Dir,Dim)——
	// 多格建筑的同一方向上可能挂多个 port（不同格子），(Dir,Dim) 会歧义。

	/** 反向方向。 */
	FORCEINLINE EDir Opposite(EDir D)
	{
		return static_cast<EDir>((static_cast<uint8>(D) + 2) & 3);
	}
}
