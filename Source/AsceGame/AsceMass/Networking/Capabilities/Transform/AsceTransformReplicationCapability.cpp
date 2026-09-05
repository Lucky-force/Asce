// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceTransformReplicationCapability.h"

#include "AsceTransformReplication.h"
#include "AsceMass/Networking/Core/AsceUnitReplicator.h"
#include "MassCommonFragments.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"

void FAsceTransformReplicationCapability::AddRequirements(FMassEntityQuery& EntityQuery)
{
	// AddRequirements内部已经是Any（见AsceReplicationPositionHandlers.h），直接转发即可。
	FAsceReplicationProcessorPositionHandler::AddRequirements(EntityQuery);
}

void FAsceTransformReplicationCapability::ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE

	// 整个Query只保证"这批entity至少命中一个已注册能力"，具体到这个chunk有没有
	// FTransformFragment还是得自己查一遍（Any同样按Optional处理访问安全性，不会crash，
	// 但不查的话我们会把TransformList当成有效数据来处理，产生垃圾数据）
	if (!Context.DoesArchetypeHaveFragment<FTransformFragment>())
	{
		return;
	}

	FAsceReplicationProcessorPositionHandler PositionHandler;
	FMassReplicationSharedFragment* RepSharedFrag = nullptr;

	auto CacheViewsCallback = [&RepSharedFrag, &PositionHandler](FMassExecutionContext& Context)
	{
		PositionHandler.CacheFragmentViews(Context);
		RepSharedFrag = &Context.GetMutableSharedFragment<FMassReplicationSharedFragment>();
		check(RepSharedFrag);
	};

	auto AddEntityCallback = [&RepSharedFrag, &PositionHandler](FMassExecutionContext& Context, const int32 EntityIdx, FAsceTransformReplicatedAgent& InReplicatedAgent, const FMassClientHandle ClientHandle) -> FMassReplicatedAgentHandle
	{
		AAsceTransformClientBubbleInfo& BubbleInfo = RepSharedFrag->GetTypedClientBubbleInfoChecked<AAsceTransformClientBubbleInfo>(ClientHandle);

		PositionHandler.AddEntity(EntityIdx, InReplicatedAgent.GetReplicatedPositionDataMutable());

		return BubbleInfo.GetAsceSerializer().Bubble.AddAgent(Context.GetEntity(EntityIdx), InReplicatedAgent);
	};

	auto ModifyEntityCallback = [&RepSharedFrag, &PositionHandler](FMassExecutionContext& Context, const int32 EntityIdx, const EMassLOD::Type LOD, const double Time, const FMassReplicatedAgentHandle Handle, const FMassClientHandle ClientHandle)
	{
		AAsceTransformClientBubbleInfo& BubbleInfo = RepSharedFrag->GetTypedClientBubbleInfoChecked<AAsceTransformClientBubbleInfo>(ClientHandle);
		auto& Bubble = BubbleInfo.GetAsceSerializer().Bubble;

		// 周期坐标同步：每次Replicator轮到这个entity都会走到这里，是否真的重新广播由
		// SetPositionFromFragment内部按位置容差判断，不是无脑每帧发。
		// TODO：如果以后要按LOD进一步降频（比如低LOD的entity几秒才发一次），可以在这里用
		// LOD/Time加一层节流；目前实测下来容差判断已经够用，暂不做。
		PositionHandler.ModifyEntity(Handle, EntityIdx, Bubble);
	};

	auto RemoveEntityCallback = [&RepSharedFrag](FMassExecutionContext& Context, const FMassReplicatedAgentHandle Handle, const FMassClientHandle ClientHandle)
	{
		AAsceTransformClientBubbleInfo& BubbleInfo = RepSharedFrag->GetTypedClientBubbleInfoChecked<AAsceTransformClientBubbleInfo>(ClientHandle);
		BubbleInfo.GetAsceSerializer().Bubble.RemoveAgentChecked(Handle);
	};

	UAsceUnitReplicator::CallCalculateClientReplication<FAsceTransformFastArrayItem>(Context, ReplicationContext, CacheViewsCallback, AddEntityCallback, ModifyEntityCallback, RemoveEntityCallback);
#endif // UE_REPLICATION_COMPILE_SERVER_CODE
}
