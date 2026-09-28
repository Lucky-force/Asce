// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceVelocityReplicationCapability.h"

#include "AsceVelocityReplication.h"
#include "MassReplicationProcessor.h"
#include "MassMovementFragments.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"

void FAsceVelocityReplicationCapability::AddRequirements(FMassEntityQuery& EntityQuery)
{
	// AddRequirements内部已经是Any（见AsceReplicationVelocityHandlers.h），直接转发即可。
	FAsceReplicationProcessorVelocityHandler::AddRequirements(EntityQuery);
}

void FAsceVelocityReplicationCapability::ProcessChunk(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext
	, const FMassClientHandle& ClientHandle, TArrayView<FMassReplicatedAgentHandle> InOutMarkers)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE

	// 整个Query只保证"这批entity至少命中一个已注册能力"，具体到这个chunk有没有
	// FMassDesiredMovementFragment还是得自己查一遍（Any同样按Optional处理访问安全性，不会crash，
	// 但不查的话我们会把DesiredMovementList当成有效数据来处理，产生垃圾数据）
	if (!Context.DoesArchetypeHaveFragment<FMassDesiredMovementFragment>())
	{
		return;
	}

	// 本能力自己的 Bubble 实例：按类型从 UMassReplicationSubsystem 查（shared fragment 里只有"代表类"那一个）
	UMassReplicationSubsystem& RepSubsystem = ReplicationContext.ReplicationSubsystem;
	const FMassBubbleInfoClassHandle BubbleClassHandle =
		RepSubsystem.GetBubbleInfoClassHandle(AAsceVelocityClientBubbleInfo::StaticClass());
	if (!BubbleClassHandle.IsValid())
	{
		// 配置错误：BubbleInfoClass 没在 bootstrap 里注册（继续走会命中引擎 check 崩）
		ensureMsgf(false, TEXT("[Replication] BubbleInfoClass 未注册：%s"), *AAsceVelocityClientBubbleInfo::StaticClass()->GetName());
		return;
	}

	FAsceVelocityClientBubbleHandler& Bubble =
		RepSubsystem.GetTypedClientBubbleChecked<AAsceVelocityClientBubbleInfo>(BubbleClassHandle, ClientHandle)
			->GetAsceSerializer().Bubble;

	FAsceReplicationProcessorVelocityHandler VelocityHandler;
	VelocityHandler.CacheFragmentViews(Context);

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
				FAsceVelocityReplicatedAgent ReplicatedAgent;
				ReplicatedAgent.SetNetID(NetworkIDList[EntityIdx].NetID);
				ReplicatedAgent.SetTemplateID(TemplateIDList[EntityIdx].ID);
				VelocityHandler.AddEntity(EntityIdx, ReplicatedAgent.GetReplicatedVelocityDataMutable());

				const FMassReplicatedAgentHandle NewHandle = Bubble.AddAgent(Context.GetEntity(EntityIdx), ReplicatedAgent);
				if (!InOutMarkers[EntityIdx].IsValid())
				{
					InOutMarkers[EntityIdx] = NewHandle;
				}
			}
			else
			{
				// 周期速度同步：是否真的重新广播由SetVelocityFromFragment内部按速度容差判断，
				// 不是无脑每帧发。TODO：如果以后要按LOD进一步降频，可以在这里加一层节流；
				// 目前实测下来容差判断已经够用，暂不做。
				VelocityHandler.ModifyEntity(ExistingHandle, EntityIdx, Bubble);
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
