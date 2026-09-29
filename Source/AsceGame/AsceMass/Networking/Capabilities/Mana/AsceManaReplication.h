// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "MassClientBubbleInfoBase.h"
#include "MassReplicationFragments.h"

#include "AsceMass/Networking/Core/AsceReplicationValueHandlers.h"
#include "AsceMass/Networking/Core/AsceValueReplicationBubbleHandler.h"
#include "AsceMass/Networking/Core/AsceLightReplicatedAgent.h"
#include "AsceMass/Combat/AsceCombatFragments.h"   // FAsceManaFragment

#include "AsceManaReplication.generated.h"

class AAsceManaClientBubbleInfo;

/**
 * 蓝量同步通道：与 Health 完全同款（见 `AsceHealthReplication.h` 的文件头说明）。
 * **同步绝对值**（MaxMana + CurrentMana 两个字段）：Max 会随装备/升级/buff 变化，
 * 静态 def 只是出生初始值 ⇒ 两个值都必须复制。
 */
USTRUCT()
struct FAsceManaReplicatedAgent : public FAsceLightReplicatedAgentBase
{
	GENERATED_BODY()

	const FReplicatedValueFloat& GetMaxManaData() const { return MaxMana; }
	FReplicatedValueFloat& GetMaxManaDataMutable() { return MaxMana; }

	const FReplicatedValueFloat& GetCurrentManaData() const { return CurrentMana; }
	FReplicatedValueFloat& GetCurrentManaDataMutable() { return CurrentMana; }

private:
	UPROPERTY(Transient)
	FReplicatedValueFloat MaxMana;

	UPROPERTY(Transient)
	FReplicatedValueFloat CurrentMana;
};

/** FastArray 条目（与 MassCrowd 的 `FCrowdFastArrayItem` 同模式） */
USTRUCT()
struct FAsceManaFastArrayItem : public FMassFastArrayItemBase
{
	GENERATED_BODY()

	FAsceManaFastArrayItem() = default;
	FAsceManaFastArrayItem(const FAsceManaReplicatedAgent& InAgent, const FMassReplicatedAgentHandle InHandle)
		: FMassFastArrayItemBase(InHandle)
		, Agent(InAgent)
	{}

	typedef FAsceManaReplicatedAgent FReplicatedAgentType;

	UPROPERTY()
	FAsceManaReplicatedAgent Agent;
};

/** 蓝量同步策略（服务器侧差量判定 + 客户端回写都由模板 `TAsceValue*` 驱动） */
struct FAsceManaPolicy
{
	typedef FAsceManaFragment FragmentType;
	using ValueType = float;
	using FValueData = FReplicatedValueFloat;
	using FAgentType = FAsceManaReplicatedAgent;
	using FFastArrayItemType = FAsceManaFastArrayItem;
	using FBubbleInfoType = AAsceManaClientBubbleInfo;

	/** 字段约定：0 = MaxMana、1 = CurrentMana（**顺序即写回顺序**） */
	static constexpr int32 NumFields = 2;

	static FValueData& GetMutableData(FAsceManaReplicatedAgent& Agent, const int32 FieldIndex)
	{
		return (FieldIndex == 0) ? Agent.GetMaxManaDataMutable() : Agent.GetCurrentManaDataMutable();
	}
	static const FValueData& GetData(const FAsceManaReplicatedAgent& Agent, const int32 FieldIndex)
	{
		return (FieldIndex == 0) ? Agent.GetMaxManaData() : Agent.GetCurrentManaData();
	}
	static float ExtractFromFragment(const FAsceManaFragment& Fragment, const int32 FieldIndex)
	{
		return (FieldIndex == 0) ? Fragment.MaxMana : Fragment.CurrentMana;
	}
	static void ApplyToFragment(FAsceManaFragment& Fragment, const int32 FieldIndex, const float Value)
	{
		if (FieldIndex == 0)
		{
			Fragment.MaxMana = Value;
		}
		else
		{
			Fragment.CurrentMana = Value;
		}
	}

	// 绝对值变化超过 0.01 才重新标脏
	static constexpr float Tolerance = 0.01f;

	static bool ValuesDiffer(const float NewValue, const float OldValue, const int32 FieldIndex)
	{
		return !FMath::IsNearlyEqual(NewValue, OldValue, Tolerance);
	}
};

using FAsceManaClientBubbleHandler = TAsceValueClientBubbleHandler<FAsceManaFastArrayItem, FAsceManaPolicy>;

/** 每个客户端一份，走 FastArraySerializer 增量同步（与 Health 同款） */
USTRUCT()
struct FAsceManaClientBubbleSerializer : public FMassClientBubbleSerializerBase
{
	GENERATED_BODY()

	FAsceManaClientBubbleSerializer()
	{
		Bubble.Initialize(Agents, *this);
	}

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FAsceManaFastArrayItem, FAsceManaClientBubbleSerializer>(Agents, DeltaParams, *this);
	}

public:
	FAsceManaClientBubbleHandler Bubble;

protected:
	UPROPERTY(Transient)
	TArray<FAsceManaFastArrayItem> Agents;
};

/**
 * **必须**声明 traits，否则引擎不会调 `NetDeltaSerialize` ⇒ FastArray 标脏不会变成 push-model 属性脏，
 * 蓝量根本不会复制。`WithCopy = false` 同时解决 handler 引用成员导致的"隐式拷贝赋值被删除"（C2280）
 * —— 不再需要手写 `operator=`。见 `Health/AsceHealthReplication.h` 同款写法。
 */
template<>
struct TStructOpsTypeTraits<FAsceManaClientBubbleSerializer> : public TStructOpsTypeTraitsBase2<FAsceManaClientBubbleSerializer>
{
	enum
	{
		WithNetDeltaSerializer = true,
		WithCopy = false,
	};
};

/** 客户端 BubbleInfo Actor：`FBubbleInfoType` 的约定 = 提供 `GetAsceSerializer()`（内含 public `Bubble`） */
UCLASS()
class AAsceManaClientBubbleInfo : public AMassClientBubbleInfoBase
{
	GENERATED_BODY()

public:
	AAsceManaClientBubbleInfo(const FObjectInitializer& ObjectInitializer);

	FAsceManaClientBubbleSerializer& GetAsceSerializer() { return AsceSerializer; }
	const FAsceManaClientBubbleSerializer& GetAsceSerializer() const { return AsceSerializer; }

protected:
	UPROPERTY(Transient, Replicated)
	FAsceManaClientBubbleSerializer AsceSerializer;
};
