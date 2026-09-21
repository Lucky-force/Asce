// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceVisionHeightMap.h"

void FAsceVisionHeightMap::Init(int32 InGridW, int32 InGridH, float InCellSizeCm)
{
	GridW = FMath::Max(1, InGridW);
	GridH = FMath::Max(1, InGridH);
	CellSizeCm = InCellSizeCm;
	StaticHeights.Reset();
	StaticHeights.SetNumZeroed(GridW * GridH);
	DynamicOcclusion.Reset();
	DynamicOcclusion.SetNumZeroed(GridW * GridH);
	OcclusionDirtyCells.Reset();

	// §2 硬约束：启动即校验（配置错误第一时间暴露，而不是等放建筑时发现错位）
	AsceVision::ValidateCellSizeConfig(CellSizeCm);
}

void FAsceVisionHeightMap::ResetStaticToZero()
{
	// 注意不能用 SetNumZeroed：尺寸不变时它是 no-op，必须显式清零
	if (StaticHeights.Num() > 0)
	{
		FMemory::Memzero(StaticHeights.GetData(), StaticHeights.Num() * sizeof(uint16));
	}
}

void FAsceVisionHeightMap::SetStaticHeightAt(int32 Index, uint16 Height)
{
	if (StaticHeights.IsValidIndex(Index))
	{
		StaticHeights[Index] = Height;
	}
}

void FAsceVisionHeightMap::SetStaticHeightAtCell(const FIntVector2& Cell, uint16 Height)
{
	if (IsInside(Cell))
	{
		StaticHeights[CellToIndex(Cell)] = Height;
	}
}

void FAsceVisionHeightMap::SetStaticHeights(TArray<uint16>&& InHeights)
{
	if (InHeights.Num() == StaticHeights.Num())
	{
		StaticHeights = MoveTemp(InHeights);
	}
	else
	{
		UE_LOG(LogVision, Error, TEXT("SetStaticHeights 尺寸不匹配：期望 %d，实际 %d"), StaticHeights.Num(), InHeights.Num());
	}
}

void FAsceVisionHeightMap::ClearDynamicOcclusion()
{
	// 整层重建的起点：先把"将要消失的非 0 旧值"全部标脏（否则 Set 阶段只比对清零后的值，
	// 察觉不到"旧的遮挡被移除了"，上层会漏做视野翻转失效）。
	if (OcclusionDirtyCells.Num() == 0 && DynamicOcclusion.Num() > 0)
	{
		// 只在 dirty 列表还是空的时候批量收集旧非 0 格；多次 Clear/重建由上层按轮 Reset。
		OcclusionDirtyCells.Reserve(OcclusionDirtyCells.Num() + DynamicOcclusion.Num());
		for (int32 Index = 0; Index < DynamicOcclusion.Num(); ++Index)
		{
			if (DynamicOcclusion[Index] != 0)
			{
				OcclusionDirtyCells.Add(Index);
			}
		}
	}
	// 注意：清零不能依赖 SetNumZeroed（数组已是满尺寸时它是 no-op），显式 Memzero
	if (DynamicOcclusion.Num() > 0)
	{
		FMemory::Memzero(DynamicOcclusion.GetData(), DynamicOcclusion.Num() * sizeof(uint16));
	}
}

void FAsceVisionHeightMap::SetDynamicOcclusionRange(int32 InX0, int32 InY0, int32 InX1, int32 InY1, uint16 OcclusionHeight)
{
	const int32 X0 = FMath::Max(0, InX0);
	const int32 Y0 = FMath::Max(0, InY0);
	const int32 X1 = FMath::Min(GridW - 1, InX1);
	const int32 Y1 = FMath::Min(GridH - 1, InY1);
	if (X0 > X1 || Y0 > Y1)
	{
		return; // footprint 完全在地图外
	}

	for (int32 Y = Y0; Y <= Y1; ++Y)
	{
		const int32 RowBase = Y * GridW;
		for (int32 X = X0; X <= X1; ++X)
		{
			const int32 Index = RowBase + X;
			if (DynamicOcclusion[Index] != OcclusionHeight)
			{
				DynamicOcclusion[Index] = OcclusionHeight;
				OcclusionDirtyCells.Add(Index);
			}
		}
	}
}

void FAsceVisionHeightMap::ClearDynamicOcclusionRect(int32 InX0, int32 InY0, int32 InX1, int32 InY1)
{
	const int32 X0 = FMath::Max(0, InX0);
	const int32 Y0 = FMath::Max(0, InY0);
	const int32 X1 = FMath::Min(GridW - 1, InX1);
	const int32 Y1 = FMath::Min(GridH - 1, InY1);
	if (X0 > X1 || Y0 > Y1)
	{
		return; // 区域完全在地图外
	}

	for (int32 Y = Y0; Y <= Y1; ++Y)
	{
		const int32 RowBase = Y * GridW;
		for (int32 X = X0; X <= X1; ++X)
		{
			const int32 Index = RowBase + X;
			if (DynamicOcclusion[Index] != 0)
			{
				DynamicOcclusion[Index] = 0;
				OcclusionDirtyCells.Add(Index);
			}
		}
	}
}


bool FAsceVisionHeightMap::SampleHeightWorld(const FVector2D& WorldXY, float& OutHeightCm) const
{
	if (!IsValid() || CellSizeCm <= 0.f)
	{
		return false;
	}

	// 世界 → 格中心网格坐标（格中心 = 整数格坐标）
	const float GX = static_cast<float>(WorldXY.X) / CellSizeCm - 0.5f;
	const float GY = static_cast<float>(WorldXY.Y) / CellSizeCm - 0.5f;
	if (GX < -0.5f || GY < -0.5f || GX > GridW - 0.5f || GY > GridH - 0.5f)
	{
		return false;   // 图外：调用方自己决定兜底
	}

	const int32 X0 = FMath::FloorToInt32(GX);
	const int32 Y0 = FMath::FloorToInt32(GY);
	const float FX = GX - static_cast<float>(X0);
	const float FY = GY - static_cast<float>(Y0);

	const int32 X0C = FMath::Clamp(X0, 0, GridW - 1);
	const int32 Y0C = FMath::Clamp(Y0, 0, GridH - 1);
	const int32 X1C = FMath::Clamp(X0 + 1, 0, GridW - 1);
	const int32 Y1C = FMath::Clamp(Y0 + 1, 0, GridH - 1);

	const float H00 = static_cast<float>(GetStaticHeight(X0C + Y0C * GridW));
	const float H10 = static_cast<float>(GetStaticHeight(X1C + Y0C * GridW));
	const float H01 = static_cast<float>(GetStaticHeight(X0C + Y1C * GridW));
	const float H11 = static_cast<float>(GetStaticHeight(X1C + Y1C * GridW));
	const float Bottom = FMath::Lerp(H00, H10, FX);
	const float Top = FMath::Lerp(H01, H11, FX);
	OutHeightCm = FMath::Lerp(Bottom, Top, FY);
	return true;
}
