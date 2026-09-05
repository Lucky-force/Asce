// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceVelocityReplicationCapability.h"

#include "AsceVelocityReplication.h"
#include "AsceMass/Networking/Core/AsceUnitReplicator.h"
#include "MassMovementFragments.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"

void FAsceVelocityReplicationCapability::AddRequirements(FMassEntityQuery& EntityQuery)
{
	// AddRequirements内部已经是Any（见AsceReplicationVelocityHandlers.h），直接转发即可。
	FAsceReplicationProcessorVelocityHandler::AddRequirements(EntityQuery);
}

void FAsceVelocityReplicationCapability::ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE

	// 整个Query只保证"这批entity至少命中一个已注册能力"，具体到这个chunk有没有
	// FMassDesiredMovementFragment还是得自己查一遍（Any同样按Optional处理访问安全性，不会crash，
	// 但不查的话我们会把DesiredMovementList当成有效数据来处理，产生垃圾数据）
	if (!Context.DoesArchetypeHaveFragment<FMassDesiredMovementFragment>())
	{
		return;
	}

	FAsceReplicationProcessorVelocityHandler VelocityHandler;
	FMassReplicationSharedFragment* RepSharedFrag = nullptr;

	auto CacheViewsCallback = [&RepSharedFrag, &VelocityHandler](FMassExecutionContext& Context)
	{
		VelocityHandler.CacheFragmentViews(Context);
		RepSharedFrag = &Context.GetMutableSharedFragment<FMassReplicationSharedFragment>();
		check(RepSharedFrag);
	};

	auto AddEntityCallback = [&RepSharedFrag, &VelocityHandler](FMassExecutionContext& Context, const int32 EntityIdx, FAsceVelocityReplicatedAgent& InReplicatedAgent, const FMassClientHandle ClientHandle) -> FMassReplicatedAgentHandle
	{
		AAsceVelocityClientBubbleInfo& BubbleInfo = RepSharedFrag->GetTypedClientBubbleInfoChecked<AAsceVelocityClientBubbleInfo>(ClientHandle);

		VelocityHandler.AddEntity(EntityIdx, InReplicatedAgent.GetReplicatedVelocityDataMutable());

		return BubbleInfo.GetAsceSerializer().Bubble.AddAgent(Context.GetEntity(EntityIdx), InReplicatedAgent);
	};

	auto ModifyEntityCallback = [&RepSharedFrag, &VelocityHandler](FMassExecutionContext& Context, const int32 EntityIdx, const EMassLOD::Type LOD, const double Time, const FMassReplicatedAgentHandle Handle, const FMassClientHandle ClientHandle)
	{
		AAsceVelocityClientBubbleInfo& BubbleInfo = RepSharedFrag->GetTypedClientBubbleInfoChecked<AAsceVelocityClientBubbleInfo>(ClientHandle);
		auto& Bubble = BubbleInfo.GetAsceSerializer().Bubble;

		// 周期速度同步：每次Replicator轮到这个entity都会走到这里，是否真的重新广播由
		// SetVelocityFromFragment内部按速度容差判断，不是无脑每帧发。
		// TODO：如果以后要按LOD进一步降频，可以在这里用LOD/Time加一层节流；
		// 目前实测下来容差判断已经够用，暂不做。
		VelocityHandler.ModifyEntity(Handle, EntityIdx, Bubble);
	};

	auto RemoveEntityCallback = [&RepSharedFrag](FMassExecutionContext& Context, const FMassReplicatedAgentHandle Handle, const FMassClientHandle ClientHandle)
	{
		AAsceVelocityClientBubbleInfo& BubbleInfo = RepSharedFrag->GetTypedClientBubbleInfoChecked<AAsceVelocityClientBubbleInfo>(ClientHandle);
		BubbleInfo.GetAsceSerializer().Bubble.RemoveAgentChecked(Handle);
	};

	UAsceUnitReplicator::CallCalculateClientReplication<FAsceVelocityFastArrayItem>(Context, ReplicationContext, CacheViewsCallback, AddEntityCallback, ModifyEntityCallback, RemoveEntityCallback);
#endif // UE_REPLICATION_COMPILE_SERVER_CODE
}
