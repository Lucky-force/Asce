// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "MassClientBubbleInfoBase.h"
#include "AsceMass/Networking/Core/AsceReplicationVelocityHandlers.h"
#include "AsceMass/Networking/Core/AsceLightReplicatedAgent.h"
#include "AsceVelocityReplication.generated.h"

/**
 * "速度同步"能力Bubble：任何entity只要挂了FMassDesiredMovementFragment，就会被
 * FAsceVelocityReplicationCapability自动捕获同步，不需要额外写任何Bubble代码。
 *
 * 之所以从坐标同步（AsceTransformReplication.h）里拆出来独立成一个Capability/FastArrayItem，
 * 是因为位置和速度的变化频率/同步及时性需求不一样，合并payload会导致谁变化都得把另一个
 * 没变的字段也搭车发一遍，详见AsceReplicationPositionHandlers.h类头注释。
 *
 * payload用int16量化存储速度（*10取整/使用时/10还原），详见AsceReplicationVelocityHandlers.h。
 *
 * 周期性同步：速度变化超过容差就重新广播（见
 * TAsceVelocityClientBubbleHandler::SetVelocityFromFragment），不是只在Add时同步一次。
 */
USTRUCT()
struct FAsceVelocityReplicatedAgent : public FAsceLightReplicatedAgentBase
{
	GENERATED_BODY()

	const FReplicatedAgentVelocityData& GetReplicatedVelocityData() const { return Velocity; }
	FReplicatedAgentVelocityData& GetReplicatedVelocityDataMutable() { return Velocity; }

private:
	UPROPERTY(Transient)
	FReplicatedAgentVelocityData Velocity;
};

/** FastArray Item，仿照MassCrowd的FCrowdFastArrayItem */
USTRUCT()
struct FAsceVelocityFastArrayItem : public FMassFastArrayItemBase
{
	GENERATED_BODY()

	FAsceVelocityFastArrayItem() = default;
	FAsceVelocityFastArrayItem(const FAsceVelocityReplicatedAgent& InAgent, const FMassReplicatedAgentHandle InHandle)
		: FMassFastArrayItemBase(InHandle)
		, Agent(InAgent)
	{}

	typedef FAsceVelocityReplicatedAgent FReplicatedAgentType;

	UPROPERTY()
	FAsceVelocityReplicatedAgent Agent;
};

/** 直接继承通用模板即可，见AsceReplicationVelocityHandlers.h类头注释 */
class FAsceVelocityClientBubbleHandler : public TAsceVelocityClientBubbleHandler<FAsceVelocityFastArrayItem>
{
};

/** 每个客户端一份，走FastArraySerializer增量同步 */
USTRUCT()
struct FAsceVelocityClientBubbleSerializer : public FMassClientBubbleSerializerBase
{
	GENERATED_BODY()

	FAsceVelocityClientBubbleSerializer()
	{
		Bubble.Initialize(Agents, *this);
	}

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FAsceVelocityFastArrayItem, FAsceVelocityClientBubbleSerializer>(Agents, DeltaParams, *this);
	}

public:
	FAsceVelocityClientBubbleHandler Bubble;

protected:
	UPROPERTY(Transient)
	TArray<FAsceVelocityFastArrayItem> Agents;
};

template<>
struct TStructOpsTypeTraits<FAsceVelocityClientBubbleSerializer> : public TStructOpsTypeTraitsBase2<FAsceVelocityClientBubbleSerializer>
{
	enum
	{
		WithNetDeltaSerializer = true,
		WithCopy = false,
	};
};

/** 每个PlayerController一份，负责真正的Actor级网络复制（Owner=对应的PlayerController） */
UCLASS()
class AAsceVelocityClientBubbleInfo : public AMassClientBubbleInfoBase
{
	GENERATED_BODY()

public:
	AAsceVelocityClientBubbleInfo(const FObjectInitializer& ObjectInitializer);

	FAsceVelocityClientBubbleSerializer& GetAsceSerializer() { return AsceSerializer; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Replicated, Transient)
	FAsceVelocityClientBubbleSerializer AsceSerializer;
};
