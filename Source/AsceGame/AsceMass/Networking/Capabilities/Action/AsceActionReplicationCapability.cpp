// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceActionReplicationCapability.h"

#include "AsceActionReplication.h"
#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"   // FAsceLifecycleFragment
#include "AsceMass/Networking/Core/AsceReplicationActionHandlers.h"
#include "MassReplicationProcessor.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"

void FAsceActionReplicationCapability::AddRequirements(FMassEntityQuery& EntityQuery)
{
	// Any：会被合并进 UAsceUnitReplicator 共用的 EntityQuery（理由见 AsceReplicationCapability.h）
	EntityQuery.AddRequirement<FAsceActionFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Any);
	// 攻击播放速度要读 Combat.AttackSpeed：**必须在这里显式声明**——DoesArchetypeHaveFragment 只查
	// archetype 有没有该 fragment，不保证 Query 绑定过视图；少了声明 GetFragmentView 会直接命中
	// CHECK_IF_VALID 的 checkf("Requested fragment type not bound")。
	// （与 AsceUnitReplicator::AddRequirements 里 FAgentRadiusFragment/FAsceGridFootprintFragment 同理）
	EntityQuery.AddRequirement<FAsceCombatFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Any);
}

void FAsceActionReplicationCapability::ProcessChunk(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext
	, const FMassClientHandle& ClientHandle, TArrayView<FMassReplicatedAgentHandle> InOutMarkers)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE

	// 整个Query只保证"这批entity至少命中一个已注册能力"，具体到这个chunk有没有
	// FAsceActionFragment还得自己查一遍（见 AsceReplicationCapability.h 的说明）
	if (!Context.DoesArchetypeHaveFragment<FAsceActionFragment>())
	{
		return;
	}

	// 本能力自己的 Bubble 实例：按类型从 UMassReplicationSubsystem 查（shared fragment 里只有"代表类"那一个）
	UMassReplicationSubsystem& RepSubsystem = ReplicationContext.ReplicationSubsystem;
	const FMassBubbleInfoClassHandle BubbleClassHandle =
		RepSubsystem.GetBubbleInfoClassHandle(AAsceActionClientBubbleInfo::StaticClass());
	if (!BubbleClassHandle.IsValid())
	{
		// 配置错误：BubbleInfoClass 没在 bootstrap 里注册（继续走会命中引擎 check 崩）
		ensureMsgf(false, TEXT("[Replication] BubbleInfoClass 未注册：%s"), *AAsceActionClientBubbleInfo::StaticClass()->GetName());
		return;
	}

	FAsceActionClientBubbleHandler& Bubble =
		RepSubsystem.GetTypedClientBubbleChecked<AAsceActionClientBubbleInfo>(BubbleClassHandle, ClientHandle)
			->GetAsceSerializer().Bubble;

	const TConstArrayView<FAsceActionFragment> ActionList = Context.GetFragmentView<FAsceActionFragment>();
	const TConstArrayView<FMassNetworkIDFragment> NetworkIDList = Context.GetFragmentView<FMassNetworkIDFragment>();
	const TConstArrayView<FReplicationTemplateIDFragment> TemplateIDList = Context.GetFragmentView<FReplicationTemplateIDFragment>();
	const TConstArrayView<FMassReplicationLODFragment> LODList = Context.GetFragmentView<FMassReplicationLODFragment>();

	// 攻击的播放速度来自 Combat.AttackSpeed（技能 v1 固定 1.0；后续按动作def的动画段算）
	TConstArrayView<FAsceCombatFragment> CombatList;
	if (Context.DoesArchetypeHaveFragment<FAsceCombatFragment>())
	{
		CombatList = Context.GetFragmentView<FAsceCombatFragment>();
	}

	// 生命周期（服务端）：Add/Remove 成功时增减 bubble 引用计数
	TArrayView<FAsceLifecycleFragment> Lifecycles;
	if (Context.DoesArchetypeHaveFragment<FAsceLifecycleFragment>())
	{
		Lifecycles = Context.GetMutableFragmentView<FAsceLifecycleFragment>();
	}

	const int32 NumEntities = Context.GetNumEntities();
	for (int32 EntityIdx = 0; EntityIdx < NumEntities; ++EntityIdx)
	{
		const FAsceActionFragment& Action = ActionList[EntityIdx];

		float PlayRate = 1.f;
		if (Action.ActionState == EAsceActionState::Attack && CombatList.Num() == NumEntities)
		{
			// 与 UAsceCombatProcessor 的 PlayRate 公式保持一致（180 → 1.0x）
			PlayRate = FMath::Max(CombatList[EntityIdx].AttackSpeed / 200.f + 0.1f, 0.01f);
		}

		const uint8 ActionStateQ = static_cast<uint8>(Action.ActionState);
		const uint8 ActionDefIdQ = static_cast<uint8>(FMath::Clamp(Action.ActionDefId, 0, 255));
		const uint8 CounterQ = static_cast<uint8>(Action.Counter & 0xFFu);
		const uint8 PlayRateQ = UE::AsceMass::Replication::QuantizeActionPlayRate(PlayRate);
		const uint8 FacingQ = UE::AsceMass::Replication::QuantizeActionFacing(Action.FacingYaw);
		// 起轴时刻（回绕百分秒）：客户端据此把时间轴标定到**服务器的**起点，而不是"收到边沿的时刻"
		const uint16 StartTimeQ = UE::AsceMass::Replication::QuantizeActionStartTime(Action.StartServerTime);

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
					FAsceActionReplicatedAgent ReplicatedAgent;
					ReplicatedAgent.SetNetID(NetworkIDList[EntityIdx].NetID);
					ReplicatedAgent.SetTemplateID(TemplateIDList[EntityIdx].ID);

					FReplicatedActionStateData& Data = ReplicatedAgent.GetReplicatedActionStateDataMutable();
					Data.SetActionState(ActionStateQ);
					Data.SetActionDefId(ActionDefIdQ);
					Data.SetCounter(CounterQ);
					Data.SetPlayRateQ(PlayRateQ);
					Data.SetFacingQ(FacingQ);
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
				// 周期同步：是否真的重新广播由 SetActionStateFromFragment 内部按字段比较判断，
				// 只在动作切换的边沿标脏，不是无脑每帧发。
				Bubble.SetActionStateFromFragment(ExistingHandle, ActionStateQ, ActionDefIdQ, CounterQ, PlayRateQ, FacingQ, StartTimeQ);
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
