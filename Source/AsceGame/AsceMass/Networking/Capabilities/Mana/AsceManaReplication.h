// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "MassClientBubbleInfoBase.h"
#include "MassReplicationFragments.h"

#include "AsceMass/Networking/Core/AsceReplicationScalarHandlers.h"
#include "AsceMass/Networking/Core/AsceScalarReplicationBubbleHandler.h"
#include "AsceMass/Combat/AsceCombatFragments.h"   // FAsceManaFragment

#include "AsceManaReplication.generated.h"

class AAsceManaClientBubbleInfo;

/**
 * 蓝量"单标量"复制通道：与 Health 完全同款（见 `AsceHealthReplication.h` 的文件头说明与新增步骤）。
 * 服务器把 `CurrentMana / MaxMana` 归一化后复制（Tolerance 0.01），客户端反算回 `CurrentMana`。
 */
USTRUCT()
struct FAsceManaReplicatedAgent : public FReplicatedAgentBase
{
	GENERATED_BODY()

	const FReplicatedScalarData& GetManaRatioData() const { return ManaRatio; }
	FReplicatedScalarData& GetManaRatioDataMutable() { return ManaRatio; }

private:
	UPROPERTY(Transient)
	FReplicatedScalarData ManaRatio;
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

/** 蓝量"比例"策略（服务器侧差量判定 + 客户端回写都由模板 `TAsceScalar*` 驱动） */
struct FAsceManaRatioPolicy
{
	typedef FAsceManaFragment FragmentType;
	using FAgentType = FAsceManaReplicatedAgent;
	using FFastArrayItemType = FAsceManaFastArrayItem;
	using FBubbleInfoType = AAsceManaClientBubbleInfo;

	static FReplicatedScalarData& GetMutableData(FAsceManaReplicatedAgent& Agent)
	{
		return Agent.GetManaRatioDataMutable();
	}
	static const FReplicatedScalarData& GetData(const FAsceManaReplicatedAgent& Agent)
	{
		return Agent.GetManaRatioData();
	}
	static float ExtractFromFragment(const FAsceManaFragment& Fragment)
	{
		return Fragment.MaxMana > KINDA_SMALL_NUMBER
			? FMath::Clamp(Fragment.CurrentMana / Fragment.MaxMana, 0.f, 1.f)
			: 0.f;
	}
	static void ApplyToFragment(FAsceManaFragment& Fragment, const float Ratio)
	{
		Fragment.CurrentMana = Fragment.MaxMana * Ratio;
	}
	static constexpr float Tolerance = 0.01f;
};

using FAsceManaClientBubbleHandler = TAsceScalarClientBubbleHandler<FAsceManaFastArrayItem, FAsceManaRatioPolicy>;

/** 每个客户端一份，走 FastArraySerializer 增量同步（与 Health 同款） */
USTRUCT()
struct FAsceManaClientBubbleSerializer : public FMassClientBubbleSerializerBase
{
	GENERATED_BODY()

	FAsceManaClientBubbleSerializer()
	{
		Bubble.Initialize(Agents, *this);
	}

	/**
	 * 通用 handler 模板里有**引用成员**（`TClientBubbleHandlerBase& OwnerHandler`）⇒ 隐式拷贝赋值被删除；
	 * 而 UHT 为这个 USTRUCT 生成的 CppStructOps 需要 `operator=`（`UE::CoreUObject::Private::Copy`）⇒ 这里手写一个：
	 * 复制条目数组后**重新 Initialize**（把 handler 的引用重新绑到 this）。
	 */
	FAsceManaClientBubbleSerializer& operator=(const FAsceManaClientBubbleSerializer& Other)
	{
		if (this != &Other)
		{
			Agents = Other.Agents;
			Bubble.Initialize(Agents, *this);
		}
		return *this;
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
