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

/** 端口的传输角色。由 **core 在 `OnInit` 设定**（器件功能的定义，**玩家不可改**）。
 *  只是元数据：建图定向时用一次 + core 自校验 + 表现层（入/出口长得不一样）；每 tick 的推流**不看**它。 */
UENUM(BlueprintType)
enum class EAsceEtherTransportType : uint8
{
	Input  = 0 UMETA(DisplayName = "Input"),   // 只收（外部边的终点）
	Output = 1 UMETA(DisplayName = "Output"),  // 只发（外部边的起点）
	Any    = 2 UMETA(DisplayName = "Any")      // 由内部布线结构决定（传输器默认）
};

/** 建筑类型：组合（传输器）还是时序（器件）。 */
UENUM(BlueprintType)
enum class EAsceEtherDeviceType : uint8
{
	Combinational = 0 UMETA(DisplayName = "Combinational"),  // 传输器：纯组合，引擎负责传播
	Sequential    = 1 UMETA(DisplayName = "Sequential")      // 器件：每 tick 由 ProcessCore 执行
};

// 器件 op 已改为**行为类**实现：见 AsceMass/Ether/AsceEtherCores.h（UAsceEtherProcessCore 的子类）。

namespace AsceEther
{
	/** 每个建筑支持的维度上限（端口维度范围 [0, MaxDims)）。 */
	constexpr int32 MaxDims = 8;

	/** 连线距离的**上限**（端口型号的 `LinkRangeCells` 是 uint8，满值 255 格 = 16 个扇区） */
	constexpr int32 MaxLinkRangeCells = 255;

	/** 方向：从"左"起顺时针。 */
	enum class EDir : uint8
	{
		Left = 0,
		Up = 1,
		Right = 2,
		Down = 3,
		Count = 4
	};

	/** 建筑角色：器件（时序）由建筑配置的 `DeviceType` + `ProcessCoreClass` 描述；传输器 = 纯组合。 */

	/** 能量（0 号 ether）的 SignalType。 */
	constexpr int16 EnergySignalType = 0;

	// 编号约定：`0` = **能量**（族 0）；其它编号（含正/互补态）由配置/玩家定义，没有"内置通用材料"。

	// ── 互补态（有符号 SignalType；最高位 = 互补标志） ──
	// 族（family）= 去掉符号位；同族异号 = 互为正/反态（合流时湮灭）。
	// 注意：**Empty（空）由 Ether == 0 表示，不是 SignalType == 0**（0 号是真实存在的"能量"）。

	/** 取互补态（+X ↔ −X）。 */
	FORCEINLINE int16 Complement(int16 T)
	{
		return static_cast<int16>(static_cast<uint16>(T) ^ 0x8000);
	}

	/** 族 id（去掉符号位）。 */
	FORCEINLINE uint16 Family(int16 T)
	{
		return static_cast<uint16>(T) & 0x7FFF;
	}

	/** 是否是互补态（负号）。 */
	FORCEINLINE bool IsComplement(int16 T)
	{
		return (static_cast<uint16>(T) & 0x8000) != 0;
	}

	/** 同族？ */
	FORCEINLINE bool IsSameFamily(int16 A, int16 B)
	{
		return Family(A) == Family(B);
	}

	// 注意：port 的"唯一标识"是**全局 PortId**（= 建筑 PortStart + 建筑内 index），不是 (Dir,Dim)——
	// 多格建筑的同一方向上可能挂多个 port（不同格子），(Dir,Dim) 会歧义。

	/** 反向方向。 */
	FORCEINLINE EDir Opposite(EDir D)
	{
		return static_cast<EDir>((static_cast<uint8>(D) + 2) & 3);
	}
}
