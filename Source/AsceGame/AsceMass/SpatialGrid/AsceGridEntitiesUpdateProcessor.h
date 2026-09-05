// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "AsceSpatialGridSubsystem.h"
#include "AsceGridEntitiesUpdateProcessor.generated.h"

#define UE_API //ASCEGAME_API

class UAsceSpatialGridSubsystem;
struct FMassEntityQuery;

// 不注册到 Mass Phase——由 UAsceSpatialGridSubsystem::Tick 按需调用，每帧开始更新所有Entity的网格位置信息
UCLASS(MinimalAPI)
class UAsceGridEntitiesUpdateProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceGridEntitiesUpdateProcessor();
	
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;

protected:
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
	
	friend class UAsceSpatialGridSubsystem;
};

#undef UE_API