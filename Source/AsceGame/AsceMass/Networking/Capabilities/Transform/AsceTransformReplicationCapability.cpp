// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceTransformReplicationCapability.h"

#include "AsceTransformReplication.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"   // FAsceLifecycleFragment
#include "AsceMass/Networking/Core/AsceReplicationInterval.h"
#include "MassReplicationProcessor.h"
#include "MassCommonFragments.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"

void FAsceTransformReplicationCapability::AddRequirements(FMassEntityQuery& EntityQuery)
{
	// 坐标需求（FTransformFragment，ReadOnly + Any）由 `UAsceUnitReplicator::AddRequirements` 统一声明
	// （共享 Query 不允许同一 fragment 重复声明），这里不需要再加。
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

	AAsceTransformClientBubbleInfo* BubbleActor =
		RepSubsystem.GetTypedClientBubbleChecked<AAsceTransformClientBubbleInfo>(BubbleClassHandle, ClientHandle);
	FAsceTransformClientBubbleHandler& Bubble = BubbleActor->GetAsceSerializer().Bubble;

	FAsceReplicationProcessorPositionHandler PositionHandler;
	PositionHandler.CacheFragmentViews(Context);

	const TConstArrayView<FMassNetworkIDFragment> NetworkIDList = Context.GetFragmentView<FMassNetworkIDFragment>();
	const TConstArrayView<FReplicationTemplateIDFragment> TemplateIDList = Context.GetFragmentView<FReplicationTemplateIDFragment>();
	const TConstArrayView<FMassReplicationLODFragment> LODList = Context.GetFragmentView<FMassReplicationLODFragment>();
	const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();

	// 按距离的更新间隔（近快远慢）：viewer = 该客户端 bubble 的 Owner PC 的 Pawn（被客户端相机焦点驱动）
	FVector ViewerLocation = FVector::ZeroVector;
	const bool bHasViewer = UE::AsceMass::Replication::ResolveViewerLocationFromBubbleOwner(BubbleActor, ViewerLocation);

	// 生命周期（服务端）：Add/Remove 成功时增减 bubble 引用计数
	TArrayView<FAsceLifecycleFragment> Lifecycles;
	if (Context.DoesArchetypeHaveFragment<FAsceLifecycleFragment>())
	{
		Lifecycles = Context.GetMutableFragmentView<FAsceLifecycleFragment>();
	}

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
				// Add：先查"待加入票"（flapping 保护）再扣额度；不满足 ⇒ 本帧不 Add（下帧重试）
				if (Bubble.TryReserveForAdd(NetworkIDList[EntityIdx].NetID))
				{
					FAsceTransformReplicatedAgent ReplicatedAgent;
					ReplicatedAgent.SetNetID(NetworkIDList[EntityIdx].NetID);
					ReplicatedAgent.SetTemplateID(TemplateIDList[EntityIdx].ID);
					PositionHandler.AddEntity(EntityIdx, ReplicatedAgent.GetReplicatedPositionDataMutable());
					Bubble.CommitAdd(NetworkIDList[EntityIdx].NetID, ReplicatedAgent, Bubble.GetNowSeconds());

					const FMassReplicatedAgentHandle NewHandle = Bubble.AddAgent(Context.GetEntity(EntityIdx), ReplicatedAgent);
					if (Lifecycles.Num() == NumEntities)
					{
						Lifecycles[EntityIdx].IncBubbleRefCount();
					}
					if (!InOutMarkers[EntityIdx].IsValid())
					{
						InOutMarkers[EntityIdx] = NewHandle;
					}
				}
			}
			else
			{
				// 额度已满 ⇒ 本帧不再算距离/发 Modify（省 CPU）；仍标记"在 bubble 里"
				if (!Bubble.IsFrameBudgetExhausted())
				{
					// 周期坐标同步："值变了没" + "按距离的更新间隔" + "公平分帧/预算" 都由 SetPositionFromFragment 内部判断，不是无脑每帧发。
					const float Interval = bHasViewer
						? UE::AsceMass::Replication::ComputeIntervalFromDistSq(
							FVector::DistSquared(TransformList[EntityIdx].GetTransform().GetLocation(), ViewerLocation))
						: 0.f;
					PositionHandler.ModifyEntity(ExistingHandle, EntityIdx, Bubble, Interval);
				}
				if (!InOutMarkers[EntityIdx].IsValid())
				{
					InOutMarkers[EntityIdx] = ExistingHandle;
				}
			}
		}
		else if (ExistingHandle.IsValid())
		{
			if (Bubble.TryReserveForRemove())
			{
				Bubble.SavePendingTicketForHandle(ExistingHandle);
				Bubble.RemoveAgent(ExistingHandle);
				if (Lifecycles.Num() == NumEntities)
				{
					Lifecycles[EntityIdx].DecBubbleRefCount();
				}
			}
		}
	}
#endif // UE_REPLICATION_COMPILE_SERVER_CODE
}
