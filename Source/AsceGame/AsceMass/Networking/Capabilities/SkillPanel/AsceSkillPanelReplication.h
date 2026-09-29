// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "MassClientBubbleInfoBase.h"
#include "MassReplicationFragments.h"

#include "AsceMass/Networking/Core/AsceReplicationValueHandlers.h"
#include "AsceMass/Networking/Core/AsceValueReplicationBubbleHandler.h"
#include "AsceMass/Networking/Core/AsceLightReplicatedAgent.h"
#include "AsceMass/Combat/AsceCombatFragments.h"   // FAsceSkillSlotsFragment

#include "AsceSkillPanelReplication.generated.h"

class AAsceSkillPanelClientBubbleInfo;

/**
 * 技能面板同步通道（`技能系统设计.md` §6.2）：**槽位配置 + 每槽 CD**，驱动客户端 UI（图标 + CD 倒计时）。
 *
 * - 载荷 = **6 个 int32 字段**：`0..2` = 槽位技能 id，`3..5` = 槽位 CD 的**百分秒回绕值**；
 * - 统一用 `FReplicatedValueInt32`：Value 模板是"一个通道一种载荷类型"，int32 对 id 与百分秒都精确
 *   （相比 §6.2 里写的 uint16，只是每字段 4 字节而不是 2 ⇒ 每单位多 6 字节，可忽略）；
 * - **CD 语义**：服务器侧 `FAsceSkillSlot::NextReadyServerTime` 是**绝对秒**，复制时转成"百分秒回绕值"
 *   （0..65535 ≈ 10 分钟一圈；0 = 可释放）⇒ 客户端与 `GetServerWorldTimeSeconds()*100` 对比即可，
 *   **纯数据、不需要时钟**；
 * - **客户端侧** `NextReadyServerTime` 存的就是这个**回绕百分秒值**（不是绝对秒）——使用方（UI/预测）
 *   按"回绕值"解释；只有服务器侧它是绝对秒。
 */
USTRUCT()
struct FAsceSkillPanelReplicatedAgent : public FAsceLightReplicatedAgentBase
{
	GENERATED_BODY()

	/** 字段数 = 槽位数 × 2（前一半 id、后一半 CD） */
	static constexpr int32 NumFields = FAsceSkillSlotsFragment::NumSkillSlots * 2;

	/** 复制载荷（**必须 UPROPERTY(Transient)**，否则 FastArray 不会序列化它） */
	UPROPERTY(Transient)
	FReplicatedValueInt32 Fields[NumFields];

	FReplicatedValueInt32& GetFieldDataMutable(const int32 FieldIndex) { return Fields[FieldIndex]; }
	const FReplicatedValueInt32& GetFieldData(const int32 FieldIndex) const { return Fields[FieldIndex]; }
};

/** FastArray 条目（与 Health/Mana 同款） */
USTRUCT()
struct FAsceSkillPanelFastArrayItem : public FMassFastArrayItemBase
{
	GENERATED_BODY()

	FAsceSkillPanelFastArrayItem() = default;
	FAsceSkillPanelFastArrayItem(const FAsceSkillPanelReplicatedAgent& InAgent, const FMassReplicatedAgentHandle InHandle)
		: FMassFastArrayItemBase(InHandle)
		, Agent(InAgent)
	{}

	typedef FAsceSkillPanelReplicatedAgent FReplicatedAgentType;

	UPROPERTY()
	FAsceSkillPanelReplicatedAgent Agent;
};

/** 面板同步策略：服务器侧差量判定 + 客户端回写都由 `TAsceValue*` 模板驱动 */
struct FAsceSkillPanelPolicy
{
	typedef FAsceSkillSlotsFragment FragmentType;
	using ValueType = int32;
	using FValueData = FReplicatedValueInt32;
	using FAgentType = FAsceSkillPanelReplicatedAgent;
	using FFastArrayItemType = FAsceSkillPanelFastArrayItem;
	using FBubbleInfoType = AAsceSkillPanelClientBubbleInfo;

	static constexpr int32 NumFields = FAsceSkillPanelReplicatedAgent::NumFields;

	static FValueData& GetMutableData(FAsceSkillPanelReplicatedAgent& Agent, const int32 FieldIndex)
	{
		return Agent.GetFieldDataMutable(FieldIndex);
	}
	static const FValueData& GetData(const FAsceSkillPanelReplicatedAgent& Agent, const int32 FieldIndex)
	{
		return Agent.GetFieldData(FieldIndex);
	}

	static int32 ExtractFromFragment(const FAsceSkillSlotsFragment& Fragment, const int32 FieldIndex)
	{
		const int32 SlotIndex = FieldIndex % FAsceSkillSlotsFragment::NumSkillSlots;
		if (FieldIndex < FAsceSkillSlotsFragment::NumSkillSlots)
		{
			return Fragment.Slots[SlotIndex].ActionId;
		}
		// CD：绝对秒 → 百分秒回绕值（<=0 视为"已就绪"）
		const double AbsoluteServerTime = Fragment.Slots[SlotIndex].NextReadyServerTime;
		return (AbsoluteServerTime <= 0.0)
			? 0
			: static_cast<int32>(FMath::Fmod(AbsoluteServerTime * 100.0, 65536.0));
	}

	static void ApplyToFragment(FAsceSkillSlotsFragment& Fragment, const int32 FieldIndex, const int32 Value)
	{
		const int32 SlotIndex = FieldIndex % FAsceSkillSlotsFragment::NumSkillSlots;
		if (FieldIndex < FAsceSkillSlotsFragment::NumSkillSlots)
		{
			Fragment.Slots[SlotIndex].ActionId = Value;
		}
		else
		{
			// 客户端侧语义 = 回绕百分秒值（见文件头注释）：UI 与 GetServerWorldTimeSeconds()*100 对比
			Fragment.Slots[SlotIndex].NextReadyServerTime = static_cast<double>(Value);
		}
	}

	/** 离散值：精确比较（模板用 `ValuesDiffer`） */
	static bool ValuesDiffer(const int32 NewValue, const int32 OldValue, const int32 FieldIndex)
	{
		return NewValue != OldValue;
	}
	static constexpr float Tolerance = 0.f;
};

using FAsceSkillPanelClientBubbleHandler = TAsceValueClientBubbleHandler<FAsceSkillPanelFastArrayItem, FAsceSkillPanelPolicy>;

/** 每个客户端一份，走 FastArraySerializer 增量同步（与 Health/Mana 同款） */
USTRUCT()
struct FAsceSkillPanelClientBubbleSerializer : public FMassClientBubbleSerializerBase
{
	GENERATED_BODY()

	FAsceSkillPanelClientBubbleSerializer()
	{
		Bubble.Initialize(Agents, *this);
	}

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FAsceSkillPanelFastArrayItem, FAsceSkillPanelClientBubbleSerializer>(Agents, DeltaParams, *this);
	}

public:
	FAsceSkillPanelClientBubbleHandler Bubble;

protected:
	UPROPERTY(Transient)
	TArray<FAsceSkillPanelFastArrayItem> Agents;
};

/** **必须**声明 traits（否则引擎不调 `NetDeltaSerialize`，FastArray 标脏不会变成 push-model 属性脏）；
 *  `WithCopy = false` 同时解决"handler 引用成员导致隐式拷贝赋值被删除"（C2280），不需要手写 `operator=` */
template<>
struct TStructOpsTypeTraits<FAsceSkillPanelClientBubbleSerializer> : public TStructOpsTypeTraitsBase2<FAsceSkillPanelClientBubbleSerializer>
{
	enum
	{
		WithNetDeltaSerializer = true,
		WithCopy = false,
	};
};

/** 客户端 BubbleInfo Actor：`FBubbleInfoType` 的约定 = 提供 `GetAsceSerializer()` */
UCLASS()
class AAsceSkillPanelClientBubbleInfo : public AMassClientBubbleInfoBase
{
	GENERATED_BODY()

public:
	AAsceSkillPanelClientBubbleInfo(const FObjectInitializer& ObjectInitializer);

	FAsceSkillPanelClientBubbleSerializer& GetAsceSerializer() { return AsceSerializer; }
	const FAsceSkillPanelClientBubbleSerializer& GetAsceSerializer() const { return AsceSerializer; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Transient, Replicated)
	FAsceSkillPanelClientBubbleSerializer AsceSerializer;
};
