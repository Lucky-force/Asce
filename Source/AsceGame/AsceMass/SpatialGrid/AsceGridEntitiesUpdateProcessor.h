// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "AsceSpatialGridSubsystem.h"
#include "AsceGridEntitiesUpdateProcessor.generated.h"

class UAsceSpatialGridSubsystem;
struct FMassEntityQuery;

// 不注册到 Mass Phase——由调度器的空间数据阶段（UAsceSpatialGridSubsystem::RunGridStages）按需调用
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
