// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceTransformReplicationCapability.h"

#include "AsceTransformReplication.h"
#include "MassReplicationProcessor.h"
#include "MassCommonFragments.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"

void FAsceTransformReplicationCapability::AddRequirements(FMassEntityQuery& EntityQuery)
{
	// AddRequirements内部已经是Any（见AsceReplicationPositionHandlers.h），直接转发即可。
	FAsceReplicationProcessorPositionHandler::AddRequirements(EntityQuery);
}

void FAsceTransformReplicationCapability::ProcessChunk(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext
	, const FMassClientHandle& ClientHandle, TArrayView<FMassReplicatedAgentHandle> InOutMarkers)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE

	// 整个Query只保证"这批entity至少命中一个已注册能力"，具体到这个chunk有没有
	// FTransformFragment还是得自己查一遍（Any同样按Optional处理访问安全性，不会crash，
	// 但不查的话我们会把TransformList当成有效数据来处理，产生垃圾数据）
	if (!Context.DoesArchetypeHaveFragment<FTransformFragment>())
	{
		return;
	}

	// 本能力自己的 Bubble 实例：按类型从 UMassReplicationSubsystem 查（shared fragment 里只有"代表类"那一个）
	UMassReplicationSubsystem& RepSubsystem = ReplicationContext.ReplicationSubsystem;
	const FMassBubbleInfoClassHandle BubbleClassHandle =
		RepSubsystem.GetBubbleInfoClassHandle(AAsceTransformClientBubbleInfo::StaticClass());
	if (!BubbleClassHandle.IsValid())
	{
		// 配置错误：BubbleInfoClass 没在 bootstrap 里注册（继续走会命中引擎 check 崩）
		ensureMsgf(false, TEXT("[Replication] BubbleInfoClass 未注册：%s"), *AAsceTransformClientBubbleInfo::StaticClass()->GetName());
		return;
	}

	FAsceTransformClientBubbleHandler& Bubble =
		RepSubsystem.GetTypedClientBubbleChecked<AAsceTransformClientBubbleInfo>(BubbleClassHandle, ClientHandle)
			->GetAsceSerializer().Bubble;

	FAsceReplicationProcessorPositionHandler PositionHandler;
	PositionHandler.CacheFragmentViews(Context);

	const TConstArrayView<FMassNetworkIDFragment> NetworkIDList = Context.GetFragmentView<FMassNetworkIDFragment>();
	const TConstArrayView<FReplicationTemplateIDFragment> TemplateIDList = Context.GetFragmentView<FReplicationTemplateIDFragment>();
	const TConstArrayView<FMassReplicationLODFragment> LODList = Context.GetFragmentView<FMassReplicationLODFragment>();

	const int32 NumEntities = Context.GetNumEntities();
	for (int32 EntityIdx = 0; EntityIdx < NumEntities; ++EntityIdx)
	{
		// 是否已经在**本能力自己的bubble**里：查自己handler的NetID表，
		// 不能看引擎共用的 AgentData.Handle（那是所有能力共享的单槽）
		const FMassReplicatedAgentHandle ExistingHandle = Bubble.FindAgentHandleByNetID(NetworkIDList[EntityIdx].NetID);

		if (LODList[EntityIdx].LOD < EMassLOD::Off)
		{
			if (!ExistingHandle.IsValid())
			{
				FAsceTransformReplicatedAgent ReplicatedAgent;
				ReplicatedAgent.SetNetID(NetworkIDList[EntityIdx].NetID);
				ReplicatedAgent.SetTemplateID(TemplateIDList[EntityIdx].ID);
				PositionHandler.AddEntity(EntityIdx, ReplicatedAgent.GetReplicatedPositionDataMutable());

				const FMassReplicatedAgentHandle NewHandle = Bubble.AddAgent(Context.GetEntity(EntityIdx), ReplicatedAgent);
				if (!InOutMarkers[EntityIdx].IsValid())
				{
					InOutMarkers[EntityIdx] = NewHandle;
				}
			}
			else
			{
				// 周期坐标同步：是否真的重新广播由SetPositionFromFragment内部按位置容差判断，
				// 不是无脑每帧发。TODO：如果以后要按LOD进一步降频（比如低LOD的entity几秒才发一次），
				// 可以在这里用LOD加一层节流；目前实测下来容差判断已经够用，暂不做。
				PositionHandler.ModifyEntity(ExistingHandle, EntityIdx, Bubble);
				if (!InOutMarkers[EntityIdx].IsValid())
				{
					InOutMarkers[EntityIdx] = ExistingHandle;
				}
			}
		}
		else if (ExistingHandle.IsValid())
		{
			Bubble.RemoveAgent(ExistingHandle);
		}
	}
#endif // UE_REPLICATION_COMPILE_SERVER_CODE
}
