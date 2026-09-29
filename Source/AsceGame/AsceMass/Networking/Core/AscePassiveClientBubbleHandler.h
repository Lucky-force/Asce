// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine/World.h"
#include "MassClientBubbleHandler.h"
#include "MassEntityView.h"
#include "MassReplicationSubsystem.h"
#include "AsceServeWindow.h"

/**
 * 被动bubble的客户端处理基类——多bubble架构（一个entity拆多个FastArray bubble）能在客户端成立的另一半。
 *
 * 背景：引擎客户端只维护一份 `UMassReplicationSubsystem::EntityInfoMap`（NetID→{Entity, ReplicationID}），
 * 且假设"一个NetID只对应一个FastArray item"（item的ReplicationID由每个数组各自发号：第一次标脏时
 * 发一个、之后不再变，见 FastArraySerializer.h）。项目按属性拆成多个bubble后，同一个NetID会有多个
 * item、各自拿到同样的ID（比如都是1）：
 *   - 第二个bubble的add进 `FindAndUpdateOrAddMassEntityInfo` 会命中
 *     `ensure(EntityInfo->ReplicationID < ReplicationID)`（MassReplicationSubsystem.cpp），
 *     并且被判为 `FoundNewerReplicationID` 直接跳过 ⇒ 该bubble的初始值丢失；
 *   - 只要某个bubble的item经历"移除→重加"拿到新ID并写进共享表，其它bubble的change会因为
 *     `stored != item.ReplicationID` 被静默丢弃（MassClientBubbleHandler.h），移除判定也会错。
 * 结论：EntityInfoMap 的 add/modify/remove 语义只能由**一个**bubble独占。
 *
 * 分工：
 *   - **owner bubble**（Transform，项目里所有复制实体都有FTransformFragment，见UAsceRTSEntityTrait）
 *     继续走引擎的 PostReplicatedAdd/Change 帮助函数：负责实体生成、NetID注册、实体销毁，
 *     以及共享表里ID的维护（它一个NetID一个item，引擎语义自洽）。
 *   - **被动bubble**（Velocity/Health/Mana/Team……）派生自本类：add/change只查
 *     `FindMassEntityInfo(NetID)`——实体在就直接写值，不在就进 PendingAgents；Tick里补写
 *     （实体是owner异步生成的，两个actor channel谁先到不保证）。PreReplicatedRemove只清自己的
 *     pending，**绝不销毁实体**（销毁是owner的事）。这样各bubble仍然各自判脏/各自广播、互不干扰。
 *
 * 派生类必须实现 ApplyAgentToEntity（把整包Agent写回自己的Fragment），参考各
 * TAsceXxxClientBubbleHandler 里的 SetModifiedEntityData。
 */
template<typename AgentArrayItem>
class TAscePassiveClientBubbleHandler : public TAsceServeWindowHandler<AgentArrayItem>, public TClientBubbleHandlerBase<AgentArrayItem>
{
public:
	using Super = TClientBubbleHandlerBase<AgentArrayItem>;
	using FAgentType = typename AgentArrayItem::FReplicatedAgentType;

	/** 服务器侧：当前世界时间（秒）——"按距离的更新间隔"用。 */
	float GetNowSeconds() const
	{
		const UWorld* World = this->Serializer ? this->Serializer->GetWorld() : nullptr;
		return World ? World->GetRealTimeSeconds() : 0.f;
	}

	/** Remove 前调用（owner/被动都可用）：把该 item 的票收回"待加入表"，让"再进来"时仍被窗口挡住。 */
	void SavePendingTicketForHandle(const FMassReplicatedAgentHandle Handle)
	{
		const FMassAgentLookupData& Lookup = this->AgentLookupArray[Handle.GetIndex()];
		const AgentArrayItem& Item = (*this->Agents)[Lookup.AgentsIdx];
		this->SavePendingTicket(Lookup.NetID, Item.Agent.GetLastServedTicket());
	}

#if UE_REPLICATION_COMPILE_CLIENT_CODE
protected:
	virtual void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize) override
	{
		// 同一批里同一个NetID可能出现多个add（移除前又来了一次add）：保留ReplicationID最大的
		// （item的ID一经发放不再改变，重加会拿到更大的ID ⇒ 最大的就是最新的）
		TMap<FMassNetworkID, int32> ChosenIndices;
		ChosenIndices.Reserve(AddedIndices.Num());
		for (const int32 Idx : AddedIndices)
		{
			const AgentArrayItem& Item = (*this->Agents)[Idx];
			int32& Chosen = ChosenIndices.FindOrAdd(Item.Agent.GetNetID(), Idx);
			if ((*this->Agents)[Chosen].ReplicationID < Item.ReplicationID)
			{
				Chosen = Idx;
			}
		}

		for (const TPair<FMassNetworkID, int32>& Pair : ChosenIndices)
		{
			const AgentArrayItem& Item = (*this->Agents)[Pair.Value];
			if (!TryApplyToExistingEntity(Item.Agent))
			{
				PendingAgents.Add(Pair.Key, Item.Agent);
			}
		}
	}

	virtual void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize) override
	{
		for (const int32 Idx : ChangedIndices)
		{
			const AgentArrayItem& Item = (*this->Agents)[Idx];
			if (!TryApplyToExistingEntity(Item.Agent))
			{
				PendingAgents.Add(Item.Agent.GetNetID(), Item.Agent);
			}
		}
	}

	virtual void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize) override
	{
		// 被动bubble不销毁实体（销毁由owner bubble负责），只清掉可能暂存的数据
		for (const int32 Idx : RemovedIndices)
		{
			PendingAgents.Remove((*this->Agents)[Idx].Agent.GetNetID());
		}
	}

	virtual void Tick(float DeltaTime) override
	{
		Super::Tick(DeltaTime);

		if (PendingAgents.Num() == 0)
		{
			return;
		}

		UWorld* World = this->Serializer->GetWorld();
		if (World == nullptr || World->GetNetMode() != NM_Client)
		{
			return;
		}

		UMassReplicationSubsystem* ReplicationSubsystem = this->Serializer->GetReplicationSubsystem();
		if (ReplicationSubsystem == nullptr)
		{
			return;
		}

		FMassEntityManager& EntityManager = this->Serializer->GetEntityManagerChecked();
		for (auto It = PendingAgents.CreateIterator(); It; ++It)
		{
			const FMassReplicationEntityInfo* EntityInfo = ReplicationSubsystem->FindMassEntityInfo(It.Key());
			if (EntityInfo && EntityInfo->Entity.IsSet() && EntityManager.IsEntityValid(EntityInfo->Entity))
			{
				ApplyAgentToEntity(FMassEntityView(EntityManager, EntityInfo->Entity), It.Value());
				It.RemoveCurrent();
			}
		}
	}

	virtual void Reset() override
	{
		Super::Reset();
		PendingAgents.Reset();
	}

	/** owner已经生成实体就直接写值（并清掉可能存在的pending）；返回false表示实体还没生成 */
	bool TryApplyToExistingEntity(const FAgentType& Agent)
	{
		UMassReplicationSubsystem* ReplicationSubsystem = this->Serializer->GetReplicationSubsystem();
		if (ReplicationSubsystem == nullptr)
		{
			return false;
		}

		const FMassReplicationEntityInfo* EntityInfo = ReplicationSubsystem->FindMassEntityInfo(Agent.GetNetID());
		if (EntityInfo == nullptr || EntityInfo->Entity.IsSet() == false)
		{
			return false;
		}

		FMassEntityManager& EntityManager = this->Serializer->GetEntityManagerChecked();
		if (!EntityManager.IsEntityValid(EntityInfo->Entity))
		{
			// 表里还留着旧句柄（实体已销毁但尚未重加）：等 owner 生成新实体后再补写
			return false;
		}

		ApplyAgentToEntity(FMassEntityView(EntityManager, EntityInfo->Entity), Agent);
		PendingAgents.Remove(Agent.GetNetID());
		return true;
	}

	/** 把整包Agent写回对应的Fragment（派生类实现） */
	virtual void ApplyAgentToEntity(const FMassEntityView& EntityView, const FAgentType& Agent) const = 0;

	/** owner还没生成实体时暂存的Agent数据（NetID→payload），Tick里实体出现后补写 */
	TMap<FMassNetworkID, FAgentType> PendingAgents;
#endif // UE_REPLICATION_COMPILE_CLIENT_CODE
};
