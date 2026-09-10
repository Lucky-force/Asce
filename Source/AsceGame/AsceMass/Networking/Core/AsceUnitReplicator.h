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

	// ── 视野复制过滤的 client→TeamKey 注册表──
	// 复制决策（ProcessClientReplication 开头）按"当前 client 的队伍可见集"过滤实体：
	// 该表把 FMassClientHandle::GetIndex() 映射到队伍 TeamKey。目前项目还没有 gameplay 层的
	// 队伍分配结构（无 GameMode/PlayerState 队伍表），这里先给静态注册 API，注册时机由
	// gameplay 层（如 GameMode::PostLogin / GameFeature）后续调用；未注册的 client 过滤自动跳过。
	static void SetClientTeam(int32 ClientIndex, int32 TeamKey);
	static void ClearAllClientTeams();
	
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
