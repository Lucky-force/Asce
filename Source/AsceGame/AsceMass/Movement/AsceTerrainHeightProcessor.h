// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "AsceTerrainHeightProcessor.generated.h"

struct FMassEntityQuery;

/**
 * 贴地：按 entity 当前 XY 在"视野格中心网格"上做双线性插值，取静态地形高度写入位置 Z。
 *
 * 采样模型（连续双线性，等价于"格内按四个象限取当前格+角方向三个邻格中心、按距离插值"）：
 *   - 采样点 = 视野格的格中心（纯静态高度场 StaticHeights，不含动态遮挡——遮挡高度是建筑高度，
 *     参与插值会让单位贴建筑时被"抬高浮起"）；
 *   - 格中心网格坐标 g = worldXY / CellCm - 0.5（格中心 = 整数）；
 *   - floor(g) 的 2×2 四个采样点按分轴距离权重混合（先水平后垂直）；
 *   - 图外不做贴地（保持原 Z），边缘用 clamp 采样。
 *
 * 并行安全：只读 StaticHeights（初始化后不变），与 FAsceFogSubsystem 的 GT 写路径（动态遮挡）无关。
 * 端：AllNetModes（两端各自采样同一份地图数据，确定性一致）。
 */
UCLASS()
class UAsceTerrainHeightProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceTerrainHeightProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
