// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassObserverProcessor.h"
#include "MassEntityQuery.h"
#include "AsceNetIDRegistryProcessor.generated.h"

/**
 * 在FMassNetworkIDFragment被Add时，把NetID -> EntityHandle登记进UAsceNetIDRegistrySubsystem。
 *
 * 关键依赖：必须在引擎自带的UMassNetworkIDFragmentInitializer(负责给NetID赋真实值)之后执行，
 * 否则读到的NetID还是默认无效值。用ExecutionOrder.ExecuteAfter显式声明这个依赖。
 */
UCLASS()
class UAsceNetIDRegistryAddProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UAsceNetIDRegistryAddProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

/**
 * 在FMassNetworkIDFragment被Remove（entity销毁）时，从注册表里摘掉，避免野指针/悬空Handle。
 */
UCLASS()
class UAsceNetIDRegistryRemoveProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UAsceNetIDRegistryRemoveProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
