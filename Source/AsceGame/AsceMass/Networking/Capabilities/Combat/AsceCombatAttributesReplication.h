// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "MassClientBubbleInfoBase.h"
#include "MassReplicationFragments.h"

#include "AsceMass/Networking/Core/AsceReplicationValueHandlers.h"
#include "AsceMass/Networking/Core/AsceValueReplicationBubbleHandler.h"
#include "AsceMass/Networking/Core/AsceLightReplicatedAgent.h"
#include "AsceMass/Combat/AsceCombatFragments.h"   // FAsceCombatAttributesFragment

#include "AsceCombatAttributesReplication.generated.h"

class AAsceCombatAttributesClientBubbleInfo;

/**
 * 攻击属性同步通道。结构和 Health 完全同款（见 `AsceHealthReplication.h` 文件头说明）。
 *
 * 为什么攻击属性要复制：客户端要拿 `AttackSpeed` 算攻击动画 PlayRate、
 * 拿 `AttackRange` 画攻击范围圈（`FAsceCombatVisualFragment` 那套只复制动作态）。
 *
 * 值通道只有 float 一种载荷，`RangeKind` / `AttackActionId` 在这里按 float 编解码
 * （枚举是 uint8 值域、ActionId 远小于 2^24，float 能精确表示）。
 */
USTRUCT()
struct FAsceCombatAttributesReplicatedAgent : public FAsceLightReplicatedAgentBase
{
	GENERATED_BODY()

	const FReplicatedValueFloat& GetAttackRangeData() const { return AttackRange; }
	FReplicatedValueFloat& GetAttackRangeDataMutable() { return AttackRange; }

	const FReplicatedValueFloat& GetAttackDamageData() const { return AttackDamage; }
	FReplicatedValueFloat& GetAttackDamageDataMutable() { return AttackDamage; }

	const FReplicatedValueFloat& GetAttackSpeedData() const { return AttackSpeed; }
	FReplicatedValueFloat& GetAttackSpeedDataMutable() { return AttackSpeed; }

	const FReplicatedValueFloat& GetRangeKindData() const { return RangeKind; }
	FReplicatedValueFloat& GetRangeKindDataMutable() { return RangeKind; }

	const FReplicatedValueFloat& GetAttackActionIdData() const { return AttackActionId; }
	FReplicatedValueFloat& GetAttackActionIdDataMutable() { return AttackActionId; }

private:
	UPROPERTY(Transient)
	FReplicatedValueFloat AttackRange;

	UPROPERTY(Transient)
	FReplicatedValueFloat AttackDamage;

	UPROPERTY(Transient)
	FReplicatedValueFloat AttackSpeed;

	UPROPERTY(Transient)
	FReplicatedValueFloat RangeKind;

	UPROPERTY(Transient)
	FReplicatedValueFloat AttackActionId;
};

/** FastArray 条目（和 MassCrowd 的 `FCrowdFastArrayItem` 同模式） */
USTRUCT()
struct FAsceCombatAttributesFastArrayItem : public FMassFastArrayItemBase
{
	GENERATED_BODY()

	FAsceCombatAttributesFastArrayItem() = default;
	FAsceCombatAttributesFastArrayItem(const FAsceCombatAttributesReplicatedAgent& InAgent, const FMassReplicatedAgentHandle InHandle)
		: FMassFastArrayItemBase(InHandle)
		, Agent(InAgent)
	{}

	typedef FAsceCombatAttributesReplicatedAgent FReplicatedAgentType;

	UPROPERTY()
	FAsceCombatAttributesReplicatedAgent Agent;
};

/** 值同步策略：字段约定 0 = AttackRange，1 = AttackDamage，2 = AttackSpeed，3 = RangeKind，4 = AttackActionId */
struct FAsceCombatAttributesPolicy
{
	typedef FAsceCombatAttributesFragment FragmentType;
	using ValueType = float;
	using FValueData = FReplicatedValueFloat;
	using FAgentType = FAsceCombatAttributesReplicatedAgent;
	using FFastArrayItemType = FAsceCombatAttributesFastArrayItem;
	using FBubbleInfoType = AAsceCombatAttributesClientBubbleInfo;

	static constexpr int32 NumFields = 5;

	static FValueData& GetMutableData(FAsceCombatAttributesReplicatedAgent& Agent, const int32 FieldIndex)
	{
		switch (FieldIndex)
		{
		case 0:  return Agent.GetAttackRangeDataMutable();
		case 1:  return Agent.GetAttackDamageDataMutable();
		case 2:  return Agent.GetAttackSpeedDataMutable();
		case 3:  return Agent.GetRangeKindDataMutable();
		default: return Agent.GetAttackActionIdDataMutable();
		}
	}
	static const FValueData& GetData(const FAsceCombatAttributesReplicatedAgent& Agent, const int32 FieldIndex)
	{
		switch (FieldIndex)
		{
		case 0:  return Agent.GetAttackRangeData();
		case 1:  return Agent.GetAttackDamageData();
		case 2:  return Agent.GetAttackSpeedData();
		case 3:  return Agent.GetRangeKindData();
		default: return Agent.GetAttackActionIdData();
		}
	}
	static float ExtractFromFragment(const FAsceCombatAttributesFragment& Fragment, const int32 FieldIndex)
	{
		switch (FieldIndex)
		{
		case 0:  return Fragment.AttackRange;
		case 1:  return Fragment.AttackDamage;
		case 2:  return Fragment.AttackSpeed;
		case 3:  return static_cast<float>(static_cast<uint8>(Fragment.RangeKind));
		default: return static_cast<float>(Fragment.AttackActionId);
		}
	}
	static void ApplyToFragment(FAsceCombatAttributesFragment& Fragment, const int32 FieldIndex, const float Value)
	{
		switch (FieldIndex)
		{
		case 0:  Fragment.AttackRange = Value; break;
		case 1:  Fragment.AttackDamage = Value; break;
		case 2:  Fragment.AttackSpeed = Value; break;
		case 3:  Fragment.RangeKind = static_cast<EAsceRangeKind>(static_cast<uint8>(FMath::RoundToInt(Value))); break;
		default: Fragment.AttackActionId = FMath::RoundToInt(Value); break;
		}
	}

	// 浮点字段 0.01 容差；枚举/ActionId 必须精确相等
	static constexpr float Tolerance = 0.01f;

	static bool ValuesDiffer(const float NewValue, const float OldValue, const int32 FieldIndex)
	{
		if (FieldIndex >= 3)
		{
			return NewValue != OldValue;
		}
		return !FMath::IsNearlyEqual(NewValue, OldValue, Tolerance);
	}
};

using FAsceCombatAttributesClientBubbleHandler = TAsceValueClientBubbleHandler<FAsceCombatAttributesFastArrayItem, FAsceCombatAttributesPolicy>;

/** 每个客户端一份，靠 FastArraySerializer 增量同步（和 Health 同款） */
USTRUCT()
struct FAsceCombatAttributesClientBubbleSerializer : public FMassClientBubbleSerializerBase
{
	GENERATED_BODY()

	FAsceCombatAttributesClientBubbleSerializer()
	{
		Bubble.Initialize(Agents, *this);
	}

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FAsceCombatAttributesFastArrayItem, FAsceCombatAttributesClientBubbleSerializer>(Agents, DeltaParams, *this);
	}

public:
	FAsceCombatAttributesClientBubbleHandler Bubble;

protected:
	UPROPERTY(Transient)
	TArray<FAsceCombatAttributesFastArrayItem> Agents;
};

template<>
struct TStructOpsTypeTraits<FAsceCombatAttributesClientBubbleSerializer> : public TStructOpsTypeTraitsBase2<FAsceCombatAttributesClientBubbleSerializer>
{
	enum
	{
		WithNetDeltaSerializer = true,
		WithCopy = false,
	};
};

/** 客户端 BubbleInfo Actor（`FBubbleInfoType` 的约定 = 提供 `GetAsceSerializer()`，内含 public `Bubble`） */
UCLASS()
class AAsceCombatAttributesClientBubbleInfo : public AMassClientBubbleInfoBase
{
	GENERATED_BODY()

public:
	AAsceCombatAttributesClientBubbleInfo(const FObjectInitializer& ObjectInitializer);

	FAsceCombatAttributesClientBubbleSerializer& GetAsceSerializer() { return AsceSerializer; }
	const FAsceCombatAttributesClientBubbleSerializer& GetAsceSerializer() const { return AsceSerializer; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Transient, Replicated)
	FAsceCombatAttributesClientBubbleSerializer AsceSerializer;
};
