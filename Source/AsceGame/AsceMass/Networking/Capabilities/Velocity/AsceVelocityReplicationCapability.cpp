// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceVelocityReplicationCapability.h"

#include "AsceVelocityReplication.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"   // FAsceLifecycleFragment
#include "AsceMass/Networking/Core/AsceReplicationInterval.h"
#include "MassReplicationProcessor.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"

void FAsceVelocityReplicationCapability::AddRequirements(FMassEntityQuery& EntityQuery)
{
	// AddRequirements内部已经是Any（见AsceReplicationVelocityHandlers.h），直接转发即可。
	FAsceReplicationProcessorVelocityHandler::AddRequirements(EntityQuery);
	// 注意：算"按距离的更新间隔"要读的 FTransformFragment 由 `UAsceUnitReplicator::AddRequirements`
	// 统一声明（共享 Query 不允许同一 fragment 重复声明），这里不要再加——重复会触发
	// Mass 的 "Duplicated requirements" checkf。
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

	AAsceVelocityClientBubbleInfo* BubbleActor =
		RepSubsystem.GetTypedClientBubbleChecked<AAsceVelocityClientBubbleInfo>(BubbleClassHandle, ClientHandle);
	FAsceVelocityClientBubbleHandler& Bubble = BubbleActor->GetAsceSerializer().Bubble;

	FAsceReplicationProcessorVelocityHandler VelocityHandler;
	VelocityHandler.CacheFragmentViews(Context);

	const TConstArrayView<FMassNetworkIDFragment> NetworkIDList = Context.GetFragmentView<FMassNetworkIDFragment>();
	const TConstArrayView<FReplicationTemplateIDFragment> TemplateIDList = Context.GetFragmentView<FReplicationTemplateIDFragment>();
	const TConstArrayView<FMassReplicationLODFragment> LODList = Context.GetFragmentView<FMassReplicationLODFragment>();

	// 按距离的更新间隔（近快远慢）：需要实体位置 + viewer（该客户端 bubble Owner PC 的 Pawn）
	TConstArrayView<FTransformFragment> TransformList;
	if (Context.DoesArchetypeHaveFragment<FTransformFragment>())
	{
		TransformList = Context.GetFragmentView<FTransformFragment>();
	}
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
					FAsceVelocityReplicatedAgent ReplicatedAgent;
					ReplicatedAgent.SetNetID(NetworkIDList[EntityIdx].NetID);
					ReplicatedAgent.SetTemplateID(TemplateIDList[EntityIdx].ID);
					VelocityHandler.AddEntity(EntityIdx, ReplicatedAgent.GetReplicatedVelocityDataMutable());
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
					// 周期速度同步："值变了没" + "按距离的更新间隔" + "公平分帧/预算" 都由 SetVelocityFromFragment 内部判断，不是无脑每帧发。
					const float Interval = (bHasViewer && TransformList.Num() == NumEntities)
						? UE::AsceMass::Replication::ComputeIntervalFromDistSq(
							FVector::DistSquared(TransformList[EntityIdx].GetTransform().GetLocation(), ViewerLocation))
						: 0.f;
					VelocityHandler.ModifyEntity(ExistingHandle, EntityIdx, Bubble, Interval);
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
