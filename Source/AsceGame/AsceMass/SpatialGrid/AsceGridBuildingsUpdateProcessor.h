// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "AsceSpatialGridSubsystem.h"
#include "AsceGridBuildingsUpdateProcessor.generated.h"

#define UE_API //ASCEGAME_API

class UAsceSpatialGridSubsystem;

/**
 * 建筑层更新 processor：把"建筑"（带 `FAsceBuildingTag` + `FAsceGridFootprintFragment` 的实体）的 footprint
 * 增量登记到 `UAsceSpatialGridSubsystem` 的建筑粗格里（内容没变则不 bump 版本）。
 *
 * 由 `UAsceSpatialGridSubsystem::RebuildBuildingsData` 每帧串行调用（建筑数量不大，不需要并行）。
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

#undef UE_API
