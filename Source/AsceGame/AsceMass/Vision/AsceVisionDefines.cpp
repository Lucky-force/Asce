// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceVisionDefines.h"

DEFINE_LOG_CATEGORY(LogVision);

namespace AsceVision
{
	void ValidateCellSizeConfig(float VisionCellSizeCm)
	{
		if (!IsVisionCellSizeValid(VisionCellSizeCm))
		{
			UE_LOG(LogVision, Error,
				TEXT("视野格尺寸 %.3fcm 不满足整除硬约束：流场格(%.3fcm)必须是视野格的整数倍。")
				TEXT("建筑 footprint 按流场格整数计，无法无损换算到视野格。")
				TEXT("请使用 %.3f / k (k=1,2,...) 系列，如 %.1fcm。"),
				VisionCellSizeCm, FlowFieldGridSizeCm, FlowFieldGridSizeCm,
				FlowFieldGridSizeCm / 2.0);
		}
	}

	namespace
	{
		/** 半径(格) → 圆行区间表。下标 = RGrid；每个表长 2*RGrid+1。 */
		TArray<TArray<FRowSpan>>& GetCircleRowSpansTable()
		{
			static TArray<TArray<FRowSpan>> Table;
			return Table;
		}

		/** 为单个 RGrid 生成行区间（y = -R..R，[ceil(-sqrt(R²-y²)), floor(sqrt(R²-y²))]）。 */
		void BuildRowSpansForRadius(int32 RGrid, TArray<FRowSpan>& OutSpans)
		{
			OutSpans.Reset();
			OutSpans.Reserve(2 * RGrid + 1);
			const double RadiusSq = static_cast<double>(RGrid) * static_cast<double>(RGrid);
			for (int32 Y = -RGrid; Y <= RGrid; ++Y)
			{
				const double HalfWidth = FMath::Sqrt(RadiusSq - static_cast<double>(Y) * static_cast<double>(Y));
				FRowSpan Span;
				Span.XStart = -FMath::FloorToInt32(HalfWidth);
				Span.XEnd = FMath::FloorToInt32(HalfWidth);
				OutSpans.Add(Span);
			}
		}
	}

	const TArray<FRowSpan>& GetCircleRowSpans(int32 RGrid)
	{
		if (RGrid < 0)
		{
			static const TArray<FRowSpan> Empty;
			return Empty;
		}

		TArray<TArray<FRowSpan>>& Table = GetCircleRowSpansTable();

		// 惰性扩容：需要时生成到 RGrid（含 RGrid==0：单格 [0,0]）
		if (Table.Num() <= RGrid)
		{
			const int32 OldSize = Table.Num();
			Table.SetNum(RGrid + 1, EAllowShrinking::No);
			for (int32 R = OldSize; R <= RGrid; ++R)
			{
				BuildRowSpansForRadius(R, Table[R]);
			}
		}

		return Table[RGrid];
	}

	void PrewarmCircleRowSpans()
	{
		// 覆盖半径理论上限内全部预热（MaxCircleRowTableRadiusGrid 已在 Defines 定义；惰性也够，
		// 预热只为避免运行期首次查询卡顿）。
		for (int32 R = 0; R <= MaxCircleRowTableRadiusGrid; ++R)
		{
			GetCircleRowSpans(R);
		}
	}
}
