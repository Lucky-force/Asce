// AsceEtherRegistrationObserverProcessor.h
#pragma once

#include "MassObserverProcessor.h"
#include "MassEntityQuery.h"
#include "AsceEtherRegistrationObserverProcessor.generated.h"

/**
 * ether 建筑注册 Observer：实体获得 FEtherBuildingFragment（标记）时把建筑登记进
 * UAsceEtherNetworkSubsystem（分配 nodeId、触发建图）；移除时注销。
 * 只在 Standalone/Server 跑（客户端不参与逻辑）。
 */
UCLASS()
class UAsceEtherRegistrationAddObserverProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()
public:
	UAsceEtherRegistrationAddObserverProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

UCLASS()
class UAsceEtherRegistrationRemoveObserverProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()
public:
	UAsceEtherRegistrationRemoveObserverProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
