// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "MassClientBubbleInfoBase.h"
#include "AsceMass/Networking/Core/AsceReplicationScalarHandlers.h"
#include "AsceMass/Networking/Core/AsceScalarReplicationBubbleHandler.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceHealthReplication.generated.h"

/**
 * "生命值同步"能力的完整实现：Agent数据 + Policy（业务转换逻辑） + Bubble
 * （FastArraySerializer + BubbleInfo Actor）。跟坐标同步（AsceTransformReplication.h，
 * 那个走的是手动实现路线，见其文件头注释）完全独立，各自的Serializer/BubbleInfo Actor，
 * entity只要挂上FAsceHealthFragment就自动获得生命值同步，不用改任何Bubble代码。
 *
 * 新增护盾/蓝量等标量属性时，在Capabilities/下新建一个同名子文件夹（如Capabilities/Shield/），
 * 整套照抄这一个文件换名字即可：
 *   1. FAsceXxxReplicatedAgent / FAsceXxxFastArrayItem —— USTRUCT，必须手写
 *      （UHT不支持通过宏/模板生成反射类型，这是引擎硬限制，不是我们没做优化）
 *   2. FAsceXxxRatioPolicy —— 手写的业务逻辑（Fragment <-> 同步值的转换），这是唯一
 *      真正该手写的地方
 *   3. using FAsceXxxClientBubbleHandler = TAsceScalarClientBubbleHandler<...> —— 一行
 *   4. FAsceXxxClientBubbleSerializer / AAsceXxxClientBubbleInfo —— USTRUCT/UCLASS，
 *      必须手写（同第1点原因），但内容是纯粹的样板，从这里复制粘贴改名字即可，不用理解
 *   5. Capabilities/Xxx/AsceXxxReplicationCapability.h 里一行using（见AsceHealthReplicationCapability.h）
 *   6. AsceReplicationBootstrapSubsystem::PostInitialize里注册一行
 *
 * 不要往FAsceHealthReplicatedAgent里塞新的标量字段——那样会导致这个属性被迫跟血量绑死
 * 同一个Bubble/同一批entity，违背"按能力自由组合Fragment"的设计初衷（详见架构文档）。
 */
class AAsceHealthClientBubbleInfo;

/** 生命值同步的Agent数据，仿照MassCrowd的FReplicatedCrowdAgent */
USTRUCT()
struct FAsceHealthReplicatedAgent : public FReplicatedAgentBase
{
	GENERATED_BODY()

	const FReplicatedScalarData& GetHealthRatioData() const { return HealthRatio; }
	FReplicatedScalarData& GetHealthRatioDataMutable() { return HealthRatio; }

private:
	UPROPERTY(Transient)
	FReplicatedScalarData HealthRatio;
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
 * 血量比例同步的业务逻辑：唯一真正该手写的地方（traits/policy模式，类似char_traits）。
 * 通用的Handler/Capability模板本身不知道具体属性是什么，全靠这里的静态函数。
 */
struct FAsceHealthRatioPolicy
{
	typedef FAsceHealthFragment FragmentType;

	// 给TAsceScalarReplicationCapability<FieldPolicy>/TAsceScalarClientBubbleHandler用的类型信息
	using FAgentType = FAsceHealthReplicatedAgent;
	using FFastArrayItemType = FAsceHealthFastArrayItem;
	using FBubbleInfoType = AAsceHealthClientBubbleInfo;

	static FReplicatedScalarData& GetMutableData(FAsceHealthReplicatedAgent& Agent)
	{
		return Agent.GetHealthRatioDataMutable();
	}

	static const FReplicatedScalarData& GetData(const FAsceHealthReplicatedAgent& Agent)
	{
		return Agent.GetHealthRatioData();
	}

	static float ExtractFromFragment(const FAsceHealthFragment& Fragment)
	{
		return Fragment.MaxHealth > KINDA_SMALL_NUMBER
			? FMath::Clamp(Fragment.CurrentHealth / Fragment.MaxHealth, 0.f, 1.f)
			: 0.f;
	}

	static void ApplyToFragment(FAsceHealthFragment& Fragment, const float Ratio)
	{
		Fragment.CurrentHealth = Fragment.MaxHealth * Ratio;
	}

	// 血量比例变化超过1%才重新标脏，避免抖动造成的高频同步
	static constexpr float Tolerance = 0.01f;
};

/** Bubble Handler本身不需要再手写任何逻辑，直接套用通用模板 */
using FAsceHealthClientBubbleHandler = TAsceScalarClientBubbleHandler<FAsceHealthFastArrayItem, FAsceHealthRatioPolicy>;

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
