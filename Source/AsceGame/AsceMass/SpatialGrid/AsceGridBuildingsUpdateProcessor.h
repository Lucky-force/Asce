// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "AsceSpatialGridSubsystem.h"
#include "AsceGridBuildingsUpdateProcessor.generated.h"

class UAsceSpatialGridSubsystem;

/**
 * 建筑层更新 processor：把"建筑"（带 `FAsceBuildingTag` + `FAsceGridFootprintFragment` 的实体）的 footprint
 * 登记到 `UAsceSpatialGridSubsystem` 的建筑粗格里（`Part.BaseZ` 从实体 Transform 读）。
 *
 * 由 `UAsceSpatialGridSubsystem::RebuildBuildingsData` 调用；该函数**每帧全清 + 并行重填**这一层
 * （与单位层同一套：`ParallelForEachEntityChunk` + 每粗格自旋锁）。
 */
UCLASS(MinimalAPI)
class UAsceGridBuildingsUpdateProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceGridBuildingsUpdateProcessor();

	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;

protected:
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;

	friend class UAsceSpatialGridSubsystem;
};
