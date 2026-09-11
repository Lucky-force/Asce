// AsceMapDims.h
#pragma once

#include "CoreMinimal.h"
#include "AsceMass/Vision/AsceVisionDefines.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"      // UAsceFogSubsystem::TileCells
#include "AsceMass/FlowField/AsceFlowFieldCores.h" // SECTOR_SIZE / GRID_SIZE

/**
 * 世界地图尺寸的统一推导（Actor / 烘焙器共用，避免公式漂移）：
 *   FlowGridW/H   = SectorCount * SECTOR_SIZE
 *   VisionGridW/H = FlowGridW/H * Scale        （Scale = 偶数因子表[VisionDetailLevel]，见 VisionDetailLevel）
 *   VisionCellCm  = GRID_SIZE / Scale
 *   扇区数会被向上取整，使 VisionGrid 为 TileCells 的整数倍（行对齐 + tile 完整）。
 */
namespace AsceMapDims
{
	/** VisionDetailLevel → Scale：GRID_SIZE 的所有 >1 偶数因子升序，第 N 个；越界 clamp。 */
	inline int32 GetVisionScale(int32 VisionDetailLevel)
	{
		const int32 GridSize = FMath::Max(1, FMath::RoundToInt32(AsceVision::FlowFieldGridSizeCm));
		TArray<int32> Scales;
		Scales.Reserve(16);
		for (int32 S = 2; S <= GridSize; S += 2)
		{
			if ((GridSize % S) == 0)
			{
				Scales.Add(S);
			}
		}
		if (Scales.Num() == 0)
		{
			return 2;
		}
		const int32 Idx = FMath::Clamp(VisionDetailLevel, 1, Scales.Num()) - 1;
		return Scales[Idx];
	}

	/** 向上取整扇区数，使 SectorCount * SECTOR_SIZE * Scale 为 AlignUnit 的倍数。 */
	inline void AlignSectorCounts(int32& X, int32& Y, int32 Scale, int32 AlignUnit)
	{
		const int32 Factor = FMath::Max(1, SECTOR_SIZE * FMath::Max(1, Scale));
		const int32 Gcd = FMath::Max(1, FMath::GreatestCommonDivisor(Factor, AlignUnit));
		const int32 Step = FMath::Max(1, AlignUnit / Gcd);
		X = FMath::Max(1, FMath::DivideAndRoundUp(X, Step) * Step);
		Y = FMath::Max(1, FMath::DivideAndRoundUp(Y, Step) * Step);
	}

	/** 一次算出全部尺寸；SectorX/Y 会按对齐结果回写。 */
	inline void Compute(int32& SectorX, int32& SectorY, int32 VisionDetailLevel,
		int32& OutFlowW, int32& OutFlowH, int32& OutVisW, int32& OutVisH, float& OutVisionCellCm)
	{
		const int32 Scale = GetVisionScale(VisionDetailLevel);
		AlignSectorCounts(SectorX, SectorY, Scale, UAsceFogSubsystem::TileCells);

		OutFlowW = SectorX * SECTOR_SIZE;
		OutFlowH = SectorY * SECTOR_SIZE;
		OutVisW = OutFlowW * Scale;
		OutVisH = OutFlowH * Scale;
		OutVisionCellCm = AsceVision::FlowFieldGridSizeCm / static_cast<float>(Scale);
	}
}
