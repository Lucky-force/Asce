// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassReplicationProcessor.h"
#include "AsceReplicationCapability.h"
#include "AsceUnitReplicator.generated.h"

/**
 * 唯一对外注册给引擎的Replicator（entity的UMassReplicationTrait::Params.ReplicatorClass
 * 统一填这个类，不要再填具体的某个能力）。
 *
 * 内部不写死任何具体字段的同步逻辑，只是把AddRequirements/ProcessClientReplication依次转发给
 * 所有已注册的FAsceReplicationCapabilityBase实现——真正的坐标/生命值同步逻辑都在各自的
 * AsceXxxReplicationCapability.h/.cpp里，新增能力时只需要新写一对文件+注册一行，
 * 不需要修改这个类本身。
 */
UCLASS()
class UAsceUnitReplicator : public UMassReplicatorBase
{
	GENERATED_BODY()

public:
	/** 在AsceReplicationBootstrapSubsystem::PostInitialize（或更早）里，每个能力调一次这个函数 */
	static void RegisterCapability(TUniquePtr<FAsceReplicationCapabilityBase> Capability);

	virtual void AddRequirements(FMassEntityQuery& EntityQuery) override;
	virtual void ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext) override;

	/**
	 * UMassReplicatorBase::CalculateClientReplication是protected的静态模板函数，只有
	 * UMassReplicatorBase自己和它的子类能调。Capability类不是UMassReplicatorBase的子类，
	 * 没法直接调，所以这里包一层public的转发，专门给各个Capability的ProcessClientReplication用。
	 */
	template<typename AgentArrayItem, typename CacheViewsCallback, typename AddEntityCallback, typename ModifyEntityCallback, typename RemoveEntityCallback>
	static void CallCalculateClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext
		, CacheViewsCallback&& CacheViews, AddEntityCallback&& AddEntity, ModifyEntityCallback&& ModifyEntity, RemoveEntityCallback&& RemoveEntity)
	{
		CalculateClientReplication<AgentArrayItem>(Context, ReplicationContext
			, Forward<CacheViewsCallback>(CacheViews), Forward<AddEntityCallback>(AddEntity)
			, Forward<ModifyEntityCallback>(ModifyEntity), Forward<RemoveEntityCallback>(RemoveEntity));
	}

private:
	static TArray<TUniquePtr<FAsceReplicationCapabilityBase>>& GetCapabilities();
};
