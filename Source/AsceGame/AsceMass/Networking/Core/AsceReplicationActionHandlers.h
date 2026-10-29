// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AscePassiveClientBubbleHandler.h"
#include "AsceServeWindow.h"
#include "MassEntityView.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceReplicationActionHandlers.generated.h"

namespace UE::AsceMass::Replication
{
	// ── 动作态 payload 的量化（见 `技能系统设计.md` §6.1）──
	// PlayRateQ：uint8 = round(PlayRate * 100)（0.01 精度，覆盖 0.1~2.0）
	// FacingQ ：uint8 = round(Yaw / 360 * 255)（约 1.4° 精度）
	FORCEINLINE uint8 QuantizeActionPlayRate(const float PlayRate)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(PlayRate * 100.f), 1, 255));
	}

	FORCEINLINE float DequantizeActionPlayRate(const uint8 PlayRateQ)
	{
		return static_cast<float>(PlayRateQ) / 100.f;
	}

	FORCEINLINE uint8 QuantizeActionFacing(const float Yaw)
	{
		const float NormalizedYaw = FMath::Fmod(FMath::Fmod(Yaw, 360.f) + 360.f, 360.f);
		return static_cast<uint8>(FMath::RoundToInt(NormalizedYaw * (255.f / 360.f)));
	}

	FORCEINLINE float DequantizeActionFacing(const uint8 FacingQ)
	{
		return static_cast<float>(FacingQ) * (360.f / 255.f);
	}

	/** 起轴时刻量化：uint16 = 服务器世界时间的**百分秒回绕值**（与技能面板 CD 同一编码；0 = 未设） */
	FORCEINLINE uint16 QuantizeActionStartTime(const float ServerWorldTimeSeconds)
	{
		if (ServerWorldTimeSeconds <= 0.f)
		{
			return 0;
		}
		return static_cast<uint16>(FMath::Fmod(static_cast<double>(ServerWorldTimeSeconds) * 100.0, 65536.0));
	}

	/** 反解成"距现在多久之前起的轴"（秒）。回绕安全，只在 RTT 量级内使用 */
	FORCEINLINE float DequantizeActionStartAge(const uint16 StartTimeQ, const float ServerNowSeconds)
	{
		if (StartTimeQ == 0)
		{
			return 0.f;
		}
		const int32 NowQ = static_cast<int32>(FMath::Fmod(static_cast<double>(ServerNowSeconds) * 100.0, 65536.0));
		const int32 DeltaQ = (NowQ - static_cast<int32>(StartTimeQ)) & 0xFFFF;
		return static_cast<float>(DeltaQ) * 0.01f;
	}
} // namespace UE::AsceMass::Replication

/**
 * "动作态"同步的payload（`技能系统设计.md` §6.1）：驱动客户端转身/动作表现，
 * 后续 VAT 动画（§9.1 ③）也读同一份数据。
 *
 * 5+1 个字段放**同一个** struct：状态与朝向一次到达，不依赖跨通道顺序
 * （否则会出现"服务器已停+攻击，但速度/位置通道还慢一帧，客户端以为自己在 Move"）。
 * 全部由服务器发（Idle/Move 也是），客户端只读。
 *
 * **不含 `UnitTypeId`**：单位类型是模板常量（`UAsceRTSEntityTrait::UniqueType` →
 * `FAsceUnitDefFragment.UniqueTypeId`），客户端 spawn 时由实体模板自带，不必每条动作消息重发。
 */
USTRUCT()
struct FReplicatedActionStateData
{
	GENERATED_BODY()

public:
	void SetActionState(const uint8 InValue) { ActionState = InValue; }
	void SetActionDefId(const uint16 InValue) { ActionDefId = InValue; }
	void SetCounter(const uint8 InValue) { Counter = InValue; }
	void SetPlayRateQ(const uint8 InValue) { PlayRateQ = InValue; }
	void SetFacingQ(const uint8 InValue) { FacingQ = InValue; }
	void SetStartTimeQ(const uint16 InValue) { StartTimeQ = InValue; }

	uint8 GetActionState() const { return ActionState; }
	uint16 GetActionDefId() const { return ActionDefId; }
	uint8 GetCounter() const { return Counter; }
	uint8 GetPlayRateQ() const { return PlayRateQ; }
	uint8 GetFacingQ() const { return FacingQ; }
	uint16 GetStartTimeQ() const { return StartTimeQ; }

private:
	/** `EAsceActionState` 的数值 */
	UPROPERTY(Transient)
	uint8 ActionState = 0;

	/** 动作定义 id（`FAsceActionDef::ActionId`；攻击/技能行 id 上限 65535） */
	UPROPERTY(Transient)
	uint16 ActionDefId = 0;

	/** 递增序号（每次下发动作 ++；uint8 回绕，够客户端"变了就播一次"判断） */
	UPROPERTY(Transient)
	uint8 Counter = 0;

	/** 播放速度量化（见 `QuantizeActionPlayRate`） */
	UPROPERTY(Transient)
	uint8 PlayRateQ = 100;

	/** 本次动作朝向量化（见 `QuantizeActionFacing`） */
	UPROPERTY(Transient)
	uint8 FacingQ = 0;

	/** Start-of-action server time (wrapped hundredths; see QuantizeActionStartTime) */
	UPROPERTY(Transient)
	uint16 StartTimeQ = 0;
};

/**
 * "动作态"客户端Bubble Handler模板（**被动bubble**：实体生成/销毁由Transform owner负责，
 * 见 `AscePassiveClientBubbleHandler.h`；这里只把收到的动作态写进客户端的
 * `FAsceCombatVisualFragment`）。
 *
 * 服务器侧：`SetActionStateFromFragment` 逐字段比较，变化才 `MarkItemDirty`
 * ⇒ **只在动作切换的边沿发**（Idle/Move/Attack/... 切换时推一次），流量极低。
 */
template<typename AgentArrayItem>
class TAsceActionClientBubbleHandler : public TAscePassiveClientBubbleHandler<AgentArrayItem>
{
public:
	using Super = TAscePassiveClientBubbleHandler<AgentArrayItem>;
	using FAgentType = typename AgentArrayItem::FReplicatedAgentType;

#if UE_REPLICATION_COMPILE_SERVER_CODE
	/** 本客户端bubble里是否已经有这个NetID的Agent（有则返回其handle，没有返回无效handle）。
	 *  服务器侧能力靠它判断 Add 还是 Modify——引擎per-entity的AgentData.Handle是所有能力
	 *  共用的单槽，没法用来区分"这个能力自己的bubble里有没有"。 */
	FMassReplicatedAgentHandle FindAgentHandleByNetID(const FMassNetworkID NetID) const
	{
		const FMassReplicatedAgentHandle* Found = this->NetworkIDToAgentHandleMap.Find(NetID);
		return Found ? *Found : FMassReplicatedAgentHandle();
	}

	/** 服务器侧：动作态变化就写进 Agent + 标脏（字段全等则不标，不是每帧推） */
	void SetActionStateFromFragment(const FMassReplicatedAgentHandle Handle, const uint8 ActionState, const uint16 ActionDefId
		, const uint8 Counter, const uint8 PlayRateQ, const uint8 FacingQ, const uint16 StartTimeQ)
	{
		check(this->AgentHandleManager.IsValidHandle(Handle));
		const int32 AgentsIdx = this->AgentLookupArray[Handle.GetIndex()].AgentsIdx;

		AgentArrayItem& Item = (*this->Agents)[AgentsIdx];
		checkf(Item.Agent.GetNetID().IsValid(), TEXT("动作态不应该在Agent还没Add的时候更新"));

		FReplicatedActionStateData& Data = Item.Agent.GetReplicatedActionStateDataMutable();
		const bool bChanged = Data.GetActionState() != ActionState
			|| Data.GetActionDefId() != ActionDefId
			|| Data.GetCounter() != Counter
			|| Data.GetPlayRateQ() != PlayRateQ
			|| Data.GetFacingQ() != FacingQ
			|| Data.GetStartTimeQ() != StartTimeQ;
		if (!bChanged)
		{
			return;
		}

		// 公平分帧 + 单帧硬预算（见 `Core/AsceServeWindow.h`）：本轮已服务过 / 名额用满 ⇒ 跳过（不写影子 ⇒ 下帧仍是候选）
		// （动作通道无按距离的 interval ⇒ 传 0）
		const float Now = this->GetNowSeconds();
		if (!this->TryReserveForSend(Item.Agent, 0.f, Now))
		{
			return;
		}

		Data.SetActionState(ActionState);
		Data.SetActionDefId(ActionDefId);
		Data.SetCounter(Counter);
		Data.SetPlayRateQ(PlayRateQ);
		Data.SetFacingQ(FacingQ);
		Data.SetStartTimeQ(StartTimeQ);
		this->Serializer->MarkItemDirty(Item);
		this->MarkSent(Item.Agent, Now);
	}
#endif // UE_REPLICATION_COMPILE_SERVER_CODE

protected:
#if UE_REPLICATION_COMPILE_CLIENT_CODE
	/** 被动bubble：把整包Agent写回客户端表现fragment（实体由owner bubble生成） */
	virtual void ApplyAgentToEntity(const FMassEntityView& EntityView, const FAgentType& Agent) const override
	{
		ApplyToFragment(EntityView, Agent.GetReplicatedActionStateData());
	}

	/** 写回 `FAsceCombatVisualFragment`；实体没有这个fragment（比如建筑）时跳过 */
	static void ApplyToFragment(const FMassEntityView& EntityView, const FReplicatedActionStateData& Data)
	{
		FAsceCombatVisualFragment* Visual = EntityView.GetFragmentDataPtr<FAsceCombatVisualFragment>();
		if (Visual == nullptr)
		{
			return;
		}

		const uint8 MaxActionState = static_cast<uint8>(EAsceActionState::Dying);
		Visual->ActionState = static_cast<EAsceActionState>(FMath::Min(Data.GetActionState(), MaxActionState));
		Visual->ActionDefId = Data.GetActionDefId();
		Visual->Counter = Data.GetCounter();
		Visual->PlayRate = UE::AsceMass::Replication::DequantizeActionPlayRate(Data.GetPlayRateQ());
		Visual->FacingYaw = UE::AsceMass::Replication::DequantizeActionFacing(Data.GetFacingQ());
		Visual->ActionStartTimeQ = Data.GetStartTimeQ();
	}
#endif // UE_REPLICATION_COMPILE_CLIENT_CODE
	// ServeWindow / TryReserveForAdd / ... 由基类 `TAsceServeWindowHandler`（经被动基类）提供
};
