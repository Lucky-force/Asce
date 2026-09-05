// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "MassClientBubbleInfoBase.h"
#include "AsceMass/Networking/Core/AsceReplicationPositionHandlers.h"
#include "AsceTransformReplication.generated.h"

/**
 * "坐标同步"能力Bubble：拆分自原来的AsceClientBubble（详见架构文档踩坑记录）。
 * 任何entity只要挂了FTransformFragment，就会被FAsceTransformReplicationCapability自动捕获同步，
 * 不需要额外写任何Bubble代码——这是几乎所有可见entity都需要的基础同步能力。
 *
 * payload只有位置，不含速度/朝向：
 * - 朝向由Client/Server本地各自按速度方向瞬时计算（见UAsceMovementOrientationProcessor），不需要同步；
 * - 速度独立拆到了FAsceVelocityReplicationCapability（见Capabilities/Velocity/AsceVelocityReplication.h），
 *   不再跟位置合并成一个payload——两者的变化频率/容差需求不一样，合并在一起会导致谁变化都得
 *   把另一个没变的字段也搭车发一遍，没法做到"谁超过自己的容差谁就单独触发同步"，详见
 *   AsceReplicationPositionHandlers.h类头注释。
 *
 * 周期性同步：位置变化超过容差就重新广播（见
 * TAscePositionClientBubbleHandler::SetPositionFromFragment），不再是只在Add时同步一次。
 */
USTRUCT()
struct FAsceTransformReplicatedAgent : public FReplicatedAgentBase
{
	GENERATED_BODY()

	const FReplicatedAgentPositionData& GetReplicatedPositionData() const { return Position; }
	FReplicatedAgentPositionData& GetReplicatedPositionDataMutable() { return Position; }

private:
	UPROPERTY(Transient)
	FReplicatedAgentPositionData Position;
};

/** FastArray Item，仿照MassCrowd的FCrowdFastArrayItem */
USTRUCT()
struct FAsceTransformFastArrayItem : public FMassFastArrayItemBase
{
	GENERATED_BODY()

	FAsceTransformFastArrayItem() = default;
	FAsceTransformFastArrayItem(const FAsceTransformReplicatedAgent& InAgent, const FMassReplicatedAgentHandle InHandle)
		: FMassFastArrayItemBase(InHandle)
		, Agent(InAgent)
	{}

	typedef FAsceTransformReplicatedAgent FReplicatedAgentType;

	UPROPERTY()
	FAsceTransformReplicatedAgent Agent;
};

/** 直接继承通用模板即可，见AsceReplicationPositionHandlers.h类头注释 */
class FAsceTransformClientBubbleHandler : public TAscePositionClientBubbleHandler<FAsceTransformFastArrayItem>
{
};

/** 每个客户端一份，走FastArraySerializer增量同步 */
USTRUCT()
struct FAsceTransformClientBubbleSerializer : public FMassClientBubbleSerializerBase
{
	GENERATED_BODY()

	FAsceTransformClientBubbleSerializer()
	{
		Bubble.Initialize(Agents, *this);
	}

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FAsceTransformFastArrayItem, FAsceTransformClientBubbleSerializer>(Agents, DeltaParams, *this);
	}

public:
	FAsceTransformClientBubbleHandler Bubble;

protected:
	UPROPERTY(Transient)
	TArray<FAsceTransformFastArrayItem> Agents;
};

template<>
struct TStructOpsTypeTraits<FAsceTransformClientBubbleSerializer> : public TStructOpsTypeTraitsBase2<FAsceTransformClientBubbleSerializer>
{
	enum
	{
		WithNetDeltaSerializer = true,
		WithCopy = false,
	};
};

/** 每个PlayerController一份，负责真正的Actor级网络复制（Owner=对应的PlayerController） */
UCLASS()
class AAsceTransformClientBubbleInfo : public AMassClientBubbleInfoBase
{
	GENERATED_BODY()

public:
	AAsceTransformClientBubbleInfo(const FObjectInitializer& ObjectInitializer);

	FAsceTransformClientBubbleSerializer& GetAsceSerializer() { return AsceSerializer; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Replicated, Transient)
	FAsceTransformClientBubbleSerializer AsceSerializer;
};
