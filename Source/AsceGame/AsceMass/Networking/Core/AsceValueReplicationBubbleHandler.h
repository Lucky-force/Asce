// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AscePassiveClientBubbleHandler.h"
#include "AsceReplicationValueHandlers.h"
#include "AsceServeWindow.h"

/**
 * 通用"值同步"客户端 Bubble Handler 模板（支持一个 Agent 多字段），跟服务器侧的
 * `TAsceValueReplicationCapability` 是配套的一对。新增一个值同步能力时，这一层不用手写：
 *
 *   using FAsceHealthClientBubbleHandler = TAsceValueClientBubbleHandler<FAsceHealthFastArrayItem, FAsceHealthPolicy>;
 *
 * 客户端侧走**被动bubble**（见 TAscePassiveClientBubbleHandler 注释）：实体生成/销毁由
 * owner bubble（Transform）负责，这里只负责把收到的Agent写回各自的Fragment
 * （`TMassClientBubbleValueHandler::SetModifiedEntityData`）。
 *
 * 之所以要把 `SetValueFromFragment` / `ApplyAgentToEntity` 收进模板，
 * 是因为 TClientBubbleHandlerBase 的 AgentHandleManager/AgentLookupArray/Agents/Serializer 都是
 * protected，只有它的直接派生类才能访问——模板本身就是直接派生类（模板参数不影响继承关系）。
 *
 * 注意：这个类不是 USTRUCT/UCLASS，不需要反射，可以放心模板化；但 AgentArrayItem
 * （FAsceXxxFastArrayItem）仍必须手写 USTRUCT（UHT 硬限制）。
 */
template<typename AgentArrayItem, typename FieldPolicy>
class TAsceValueClientBubbleHandler : public TAscePassiveClientBubbleHandler<AgentArrayItem>
{
public:
	using Super = TAscePassiveClientBubbleHandler<AgentArrayItem>;
	using FValueHandler = TMassClientBubbleValueHandler<AgentArrayItem, FieldPolicy>;
	using FAgentType = typename AgentArrayItem::FReplicatedAgentType;

	TAsceValueClientBubbleHandler()
		: ValueHandler(*this)
	{}

#if UE_REPLICATION_COMPILE_SERVER_CODE
	/** 服务器侧：Fragment 变化时，逐字段 `ValuesDiffer` 判脏（任一字段变了才写 Agent + 标脏一次） */
	void SetValueFromFragment(const FMassReplicatedAgentHandle Handle, const typename FieldPolicy::FragmentType& Fragment)
	{
		check(this->AgentHandleManager.IsValidHandle(Handle));
		const int32 AgentsIdx = this->AgentLookupArray[Handle.GetIndex()].AgentsIdx;

		AgentArrayItem& Item = (*this->Agents)[AgentsIdx];
		checkf(Item.Agent.GetNetID().IsValid(), TEXT("值同步不应该在Agent还没Add的时候更新"));

		// ① 先判断有没有变化（没变就不占用名额/服务号）
		bool bAnyChanged = false;
		for (int32 FieldIndex = 0; FieldIndex < FieldPolicy::NumFields; ++FieldIndex)
		{
			const typename FieldPolicy::ValueType NewValue = FieldPolicy::ExtractFromFragment(Fragment, FieldIndex);
			if (FieldPolicy::ValuesDiffer(NewValue, FieldPolicy::GetData(Item.Agent, FieldIndex).GetValue(), FieldIndex))
			{
				bAnyChanged = true;
				break;
			}
		}
		if (!bAnyChanged)
		{
			return;
		}

		// ② 公平分帧 + 单帧硬预算（见 `AsceServeWindow.h`）：本轮已服务过 / 名额用满 ⇒ 跳过。
		//    跳过 = **不写影子副本** ⇒ 下帧比较仍不等 ⇒ 仍是候选（不丢、下轮补）。（值通道无按距离的 interval ⇒ 传 0）
		const float Now = this->GetNowSeconds();
		if (!this->TryReserveForSend(Item.Agent, 0.f, Now))
		{
			return;
		}

		// ③ 写字段 + 标脏 + 记时间/服务号
		for (int32 FieldIndex = 0; FieldIndex < FieldPolicy::NumFields; ++FieldIndex)
		{
			FieldPolicy::GetMutableData(Item.Agent, FieldIndex)
				.SetValue(FieldPolicy::ExtractFromFragment(Fragment, FieldIndex));
		}
		this->Serializer->MarkItemDirty(Item);
		this->MarkSent(Item.Agent, Now);
	}

	/** 本客户端bubble里是否已经有这个NetID的Agent（有则返回其handle，没有返回无效handle）。
	 *  服务器侧能力靠它判断 Add 还是 Modify——引擎per-entity的AgentData.Handle是所有能力
	 *  共用的单槽，没法用来区分"这个能力自己的bubble里有没有"。 */
	FMassReplicatedAgentHandle FindAgentHandleByNetID(const FMassNetworkID NetID) const
	{
		const FMassReplicatedAgentHandle* Found = this->NetworkIDToAgentHandleMap.Find(NetID);
		return Found ? *Found : FMassReplicatedAgentHandle();
	}
	// TryReserveForAdd()/IsFrameBudgetExhausted() 由基类 `TAsceServeWindowHandler` 提供
#endif // UE_REPLICATION_COMPILE_SERVER_CODE

protected:
#if UE_REPLICATION_COMPILE_CLIENT_CODE
	/** 被动bubble：把整包Agent写回Fragment（实体由owner bubble生成，见基类注释） */
	virtual void ApplyAgentToEntity(const FMassEntityView& EntityView, const FAgentType& Agent) const override
	{
		FValueHandler::SetModifiedEntityData(EntityView, Agent);
	}
#endif // UE_REPLICATION_COMPILE_CLIENT_CODE

	FValueHandler ValueHandler;
	// ServeWindow 由基类 `TAsceServeWindowHandler` 提供
};
