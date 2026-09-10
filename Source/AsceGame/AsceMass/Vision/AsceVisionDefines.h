// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

// 视野模块统一日志类别（定义见 AsceVisionDefines.cpp，其它 Vision 文件 include 本头即可用）
DECLARE_LOG_CATEGORY_EXTERN(LogVision, Log, All);

namespace AsceVision
{
	/** 与流场格 GRID_SIZE=100.f 对应的常量副本。
	 *  视野模块刻意不 include 流场模块头（解耦，见架构文档 §1）；此值须与流场 GRID_SIZE 一致，
	 *  且 VisionCellSize 必须是它的整数分之一（§2 硬约束，启动时 ValidateCellSizeConfig 校验）。 */
	constexpr float FlowFieldGridSizeCm = 100.f;

	/** 视野格默认边长：GRID_SIZE / k（k=2 → 50cm）。 */
	constexpr float DefaultVisionCellSizeCm = 50.f;

	/** 圆行区间表支持的覆盖半径上限（格）。R_cov 换算成格超过此值会 clamp（实现按需调大）。 */
	constexpr int32 MaxCircleRowTableRadiusGrid = 128;

	// ── 世界坐标 ↔ 视野格坐标换算（世界原点与视野网格原点都在 (0,0)） ──

	FORCEINLINE FIntVector2 WorldToVisionCell(const FVector2D& WorldPos, float CellSizeCm)
	{
		return FIntVector2(
			FMath::FloorToInt32(WorldPos.X / CellSizeCm),
			FMath::FloorToInt32(WorldPos.Y / CellSizeCm));
	}

	// ── 队伍键（打包：高16位 index = 加入顺序，低16位 id = 稳定标识） ──
	/** 队伍数上限（"最多十几个"）：TeamProjections/可见结构按此固定分配；index 必须 < 该值。 */
	constexpr int32 MaxTeamCount = 16;

	FORCEINLINE int32 PackTeamKey(int32 Index, int32 Id)
	{
		return (Index << 16) | (Id & 0xFFFF);
	}

	/** 拆出 index（== TeamKey >> 16）。注意：传入非法/负数 TeamKey 时会得到 <0 或溢出值，调用方需防御。 */
	FORCEINLINE int32 GetTeamIndex(int32 TeamKey) { return TeamKey >> 16; }

	/** 拆出 id（== TeamKey & 0xFFFF）。 */
	FORCEINLINE int32 GetTeamId(int32 TeamKey) { return TeamKey & 0xFFFF; }

	/** entity 覆盖半径（物理半径，厘米）→ 圆区间表半径（格，向上取整保证覆盖不缩小） */
	FORCEINLINE int32 CoverageRadiusToGrid(float RadiusCm, float CellSizeCm)
	{
		return FMath::Max(0, FMath::CeilToInt32(RadiusCm / CellSizeCm));
	}

	/** 硬约束：一个 cost 格必须是整数个视野格，否则建筑 footprint（按 cost 格整数计）无法无损换算。 */
	FORCEINLINE bool IsVisionCellSizeValid(float VisionCellSizeCm)
	{
		if (VisionCellSizeCm <= 0.f || FlowFieldGridSizeCm <= 0.f)
		{
			return false;
		}
		const float Ratio = FlowFieldGridSizeCm / VisionCellSizeCm;
		const float Rounded = FMath::RoundToFloat(Ratio);
		return FMath::IsNearlyEqual(Ratio, Rounded, 1e-3f);
	}

	/** 启动校验：不满足整除约束时 UE_LOG(Error)，不返回（配合 ensure 在开发期暴露配置错误）。 */
	void ValidateCellSizeConfig(float VisionCellSizeCm);

	// ── 圆行区间表（entity 覆盖圆 → 每 y 行的 [xStart, xEnd] 格区间） ──

	/** 一行圆内区间，Y 为该行相对圆心的行偏移（-R..R），XStart/XEnd 为相对圆心列的闭区间。 */
	struct FRowSpan
	{
		int32 XStart = 0;
		int32 XEnd = 0;
	};

	/** 取半径为 RGrid（格）的圆行区间表（惰性生成并缓存，线程不安全，只在初始化/游戏线程用）。
	 *  返回 TArray 的长度 = 2*RGrid+1，下标 i 对应行偏移 Y = i - RGrid。
	 *  RGrid == 0 时返回单格区间（退化覆盖）。 */
	const TArray<FRowSpan>& GetCircleRowSpans(int32 RGrid);

	/** 半径档预生成：把实现里所有会用到 R_cov 档位先预热（可选，非必须——GetCircleRowSpans 惰性）。 */
	void PrewarmCircleRowSpans();
}
