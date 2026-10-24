// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AsceStateTreeNotifyProcessor.h"

#include "Engine/World.h"
#include "MassEntityManager.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonTypes.h"                 // UE::Mass::ProcessorGroupNames::Behavior（在 MassCommon，不在 DependencySolver）
#include "MassStateTreeFragments.h"

#include "AsceGameplayTags.h"
#include "AsceMass/Behavior/AsceBehaviorFragment.h"
#include "AsceMass/Behavior/AsceStateTreeEvents.h"
#include "AsceMass/Behavior/AsceTimelineLibrary.h"
#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Networking/Core/AsceReplicationActionHandlers.h"   // DequantizeActionStartAge（锚点反解）

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceStateTreeNotifyProcessor)

namespace
{
	/** 动作态 → 行为树事件 tag（`Casting/Channel` 同一分支；`Dying` 不进树 ⇒ 返回空 tag = 不通知） */
	FGameplayTag ResolveActionEventTag(const EAsceActionState State)
	{
		switch (State)
		{
		case EAsceActionState::Move:    return AsceGameplayTags::Action_Move;
		case EAsceActionState::Attack:  return AsceGameplayTags::Action_Attack;
		case EAsceActionState::Casting:
		case EAsceActionState::Channel: return AsceGameplayTags::Action_Cast;
		case EAsceActionState::Dying:   return FGameplayTag();
		case EAsceActionState::Idle:
		default:                        return AsceGameplayTags::Action_Idle;
		}
	}

	/** 边沿比较用的**归一状态**（`Casting` 与 `Channel` 视为同一个，避免施法进循环时多发一次事件） */
	uint8 NormalizeActionState(const EAsceActionState State)
	{
		return (State == EAsceActionState::Channel)
			? static_cast<uint8>(EAsceActionState::Casting)
			: static_cast<uint8>(State);
	}
}

UAsceStateTreeNotifyProcessor::UAsceStateTreeNotifyProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllNetModes);

	// 必须在行为组（树的 tick）之前：本帧发的信号/事件本帧就能被树消费
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Behavior);
}

void UAsceStateTreeNotifyProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	// 只有"有行为树"的实体才需要通知（树 fragments 是门槛）
	EntityQuery.AddRequirement<FMassStateTreeInstanceFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAsceBehaviorFragment>(EMassFragmentAccess::ReadWrite);
	// 服务器看权威动作态、客户端看复制下来的表现态（二选一）
	EntityQuery.AddRequirement<FAsceActionFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery.AddRequirement<FAsceCombatVisualFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
}

void UAsceStateTreeNotifyProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	if (!World)
	{
		return;
	}
	const bool bAuthority = (World->GetNetMode() != NM_Client);

	EntityQuery.ForEachEntityChunk(Context, [&EntityManager, World, bAuthority](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FAsceActionFragment> Actions = Ctx.GetFragmentView<FAsceActionFragment>();
		const TConstArrayView<FAsceCombatVisualFragment> Visuals = Ctx.GetFragmentView<FAsceCombatVisualFragment>();
		const TArrayView<FAsceBehaviorFragment> Behaviors = Ctx.GetMutableFragmentView<FAsceBehaviorFragment>();
		const TConstArrayView<FMassEntityHandle> Entities = Ctx.GetEntities();
		const int32 Num = Ctx.GetNumEntities();

		for (int32 i = 0; i < Num; ++i)
		{
			// 来源按端选：服务器权威态 / 客户端表现态（缺对应 fragment 就跳过这一项）
			EAsceActionState State = EAsceActionState::Idle;
			uint32 Counter = 0;
			float ActionStartWorldTime = 0.f;   // 起轴时刻（服务器世界时间）
			const float ServerNow = static_cast<float>(AsceTimeline::GetServerNow(*World));
			if (bAuthority)
			{
				if (Actions.Num() != Num)
				{
					continue;
				}
				State = Actions[i].ActionState;
				Counter = Actions[i].Counter;
				ActionStartWorldTime = Actions[i].StartServerTime;
			}
			else
			{
				if (Visuals.Num() != Num)
				{
					continue;
				}
				State = Visuals[i].ActionState;
				Counter = Visuals[i].Counter;
				// 客户端：用复制下来的锚点反解成"起轴时刻"，而不是"收到边沿的时刻"（≈ 硬切）
				ActionStartWorldTime = ServerNow
					- UE::AsceMass::Replication::DequantizeActionStartAge(Visuals[i].ActionStartTimeQ, ServerNow);
			}

			FAsceBehaviorFragment& Behavior = Behaviors[i];
			const uint8 NormalizedState = NormalizeActionState(State);
			if (NormalizedState == Behavior.GetLastNotifiedActionState() && static_cast<uint16>(Counter) == Behavior.LastNotifiedCounter)
			{
				continue;   // 没变化
			}
			Behavior.SetLastNotifiedActionState(NormalizedState);
			Behavior.LastNotifiedCounter = static_cast<uint16>(Counter);

			// 时间轴标量：进入 Casting/Channel 起轴（已有轴就不重起，Channel 延续同一条轴）；回其它状态清掉
			{
				const bool bIsCast = (State == EAsceActionState::Casting || State == EAsceActionState::Channel);
				if (bIsCast)
				{
					if (!Behavior.IsTimelineActive())
					{
						// 用**服务器的**起轴时刻（锚点）；没拿到锚点就退回"现在"
						Behavior.TimelineStartWorldTime = (ActionStartWorldTime > 0.f) ? ActionStartWorldTime : ServerNow;
						Behavior.SetTimelineActive(true);
					}
				}
				else
				{
					Behavior.SetTimelineActive(false);
				}
			}

			const FGameplayTag EventTag = ResolveActionEventTag(State);
			if (EventTag.IsValid())
			{
				AsceStateTree::SendEventToEntity(*World, EntityManager, Entities[i], EventTag, FConstStructView(), TEXT("AsceActionNotify"));
			}
		}
	});
}
