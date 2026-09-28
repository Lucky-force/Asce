// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "MassClientBubbleInfoBase.h"
#include "AsceMass/Networking/Core/AsceReplicationValueHandlers.h"
#include "AsceMass/Networking/Core/AsceValueReplicationBubbleHandler.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceHealthReplication.generated.h"

/**
 * "生命值同步"能力的完整实现：Agent数据 + Policy（业务转换逻辑） + Bubble
 * （FastArraySerializer + BubbleInfo Actor）。跟坐标同步（AsceTransformReplication.h，
 * 那个走的是手动实现路线，见其文件头注释）完全独立，各自的Serializer/BubbleInfo Actor，
 * entity只要挂上FAsceHealthFragment就自动获得生命值同步，不用改任何Bubble代码。
 *
 * **同步的是绝对值**（MaxHealth + CurrentHealth 两个字段）：Max 会随 buff/装备/升级变化，
 * 静态 def 只能给出生初始值，不能当运行时真相 ⇒ 两个值都必须复制。
 *
 * 新增护盾/其它属性时不要往这个 Agent 里塞字段（会跟血量绑死同一个 Bubble）——照
 * `Core/AsceReplicationValueHandlers.h` 的文件头步骤新开一条值通道即可。
 */
class AAsceHealthClientBubbleInfo;

/** 生命值同步的Agent数据（两个绝对值字段：0 = MaxHealth、1 = CurrentHealth） */
USTRUCT()
struct FAsceHealthReplicatedAgent : public FReplicatedAgentBase
{
	GENERATED_BODY()

	const FReplicatedValueFloat& GetMaxHealthData() const { return MaxHealth; }
	FReplicatedValueFloat& GetMaxHealthDataMutable() { return MaxHealth; }

	const FReplicatedValueFloat& GetCurrentHealthData() const { return CurrentHealth; }
	FReplicatedValueFloat& GetCurrentHealthDataMutable() { return CurrentHealth; }

private:
	UPROPERTY(Transient)
	FReplicatedValueFloat MaxHealth;

	UPROPERTY(Transient)
	FReplicatedValueFloat CurrentHealth;
};

/** FastArray Item，仿照MassCrowd的FCrowdFastArrayItem */
USTRUCT()
struct FAsceHealthFastArrayItem : public FMassFastArrayItemBase
{
	GENERATED_BODY()

	FAsceHealthFastArrayItem() = default;
	FAsceHealthFastArrayItem(const FAsceHealthReplicatedAgent& InAgent, const FMassReplicatedAgentHandle InHandle)
		: FMassFastArrayItemBase(InHandle)
		, Agent(InAgent)
	{}

	typedef FAsceHealthReplicatedAgent FReplicatedAgentType;

	UPROPERTY()
	FAsceHealthReplicatedAgent Agent;
};

/**
 * 生命值同步的业务逻辑：唯一真正该手写的地方（traits/policy模式，类似char_traits）。
 * 通用的Handler/Capability模板本身不知道具体属性是什么，全靠这里的静态函数。
 *
 * 字段约定：0 = MaxHealth、1 = CurrentHealth（**顺序即写回顺序**，以后加依赖字段时把被依赖的放前面）。
 */
struct FAsceHealthPolicy
{
	typedef FAsceHealthFragment FragmentType;

	/** 给 TAsceValueReplicationCapability<FieldPolicy>/TAsceValueClientBubbleHandler 用的类型信息 */
	using ValueType = float;
	using FValueData = FReplicatedValueFloat;
	using FAgentType = FAsceHealthReplicatedAgent;
	using FFastArrayItemType = FAsceHealthFastArrayItem;
	using FBubbleInfoType = AAsceHealthClientBubbleInfo;

	static constexpr int32 NumFields = 2;

	static FValueData& GetMutableData(FAsceHealthReplicatedAgent& Agent, const int32 FieldIndex)
	{
		return (FieldIndex == 0) ? Agent.GetMaxHealthDataMutable() : Agent.GetCurrentHealthDataMutable();
	}

	static const FValueData& GetData(const FAsceHealthReplicatedAgent& Agent, const int32 FieldIndex)
	{
		return (FieldIndex == 0) ? Agent.GetMaxHealthData() : Agent.GetCurrentHealthData();
	}

	static float ExtractFromFragment(const FAsceHealthFragment& Fragment, const int32 FieldIndex)
	{
		return (FieldIndex == 0) ? Fragment.MaxHealth : Fragment.CurrentHealth;
	}

	static void ApplyToFragment(FAsceHealthFragment& Fragment, const int32 FieldIndex, const float Value)
	{
		if (FieldIndex == 0)
		{
			Fragment.MaxHealth = Value;
		}
		else
		{
			Fragment.CurrentHealth = Value;
		}
	}

	// 绝对值变化超过 0.01 才重新标脏（避免浮点抖动造成的高频同步）
	static constexpr float Tolerance = 0.01f;

	static bool ValuesDiffer(const float NewValue, const float OldValue, const int32 FieldIndex)
	{
		return !FMath::IsNearlyEqual(NewValue, OldValue, Tolerance);
	}
};

/** Bubble Handler本身不需要再手写任何逻辑，直接套用通用模板 */
using FAsceHealthClientBubbleHandler = TAsceValueClientBubbleHandler<FAsceHealthFastArrayItem, FAsceHealthPolicy>;

/** 每个客户端一份，走FastArraySerializer增量同步 */
USTRUCT()
struct FAsceHealthClientBubbleSerializer : public FMassClientBubbleSerializerBase
{
	GENERATED_BODY()

	FAsceHealthClientBubbleSerializer()
	{
		Bubble.Initialize(Agents, *this);
	}

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FAsceHealthFastArrayItem, FAsceHealthClientBubbleSerializer>(Agents, DeltaParams, *this);
	}

public:
	FAsceHealthClientBubbleHandler Bubble;

protected:
	UPROPERTY(Transient)
	TArray<FAsceHealthFastArrayItem> Agents;
};

template<>
struct TStructOpsTypeTraits<FAsceHealthClientBubbleSerializer> : public TStructOpsTypeTraitsBase2<FAsceHealthClientBubbleSerializer>
{
	enum
	{
		WithNetDeltaSerializer = true,
		WithCopy = false,
	};
};

/** 每个PlayerController一份，负责真正的Actor级网络复制（Owner=对应的PlayerController） */
UCLASS()
class AAsceHealthClientBubbleInfo : public AMassClientBubbleInfoBase
{
	GENERATED_BODY()

public:
	AAsceHealthClientBubbleInfo(const FObjectInitializer& ObjectInitializer);

	FAsceHealthClientBubbleSerializer& GetAsceSerializer() { return AsceSerializer; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Replicated, Transient)
	FAsceHealthClientBubbleSerializer AsceSerializer;
};
