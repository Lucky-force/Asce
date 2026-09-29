// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "MassClientBubbleInfoBase.h"
#include "AsceMass/Networking/Core/AsceReplicationActionHandlers.h"
#include "AsceMass/Networking/Core/AsceLightReplicatedAgent.h"
#include "AsceActionReplication.generated.h"

/**
 * "动作态同步"能力Bubble：挂 `FAsceActionFragment` 的entity（当前 = 所有单位）自动同步动作态/朝向。
 *
 * payload = `FReplicatedActionStateData`（5×uint8：ActionState/ActionDefId/Counter/PlayRateQ/FacingQ；
 * **不含 UnitTypeId** —— 单位类型由模板自带，见 `AsceReplicationActionHandlers.h`），
 * 客户端写回 `FAsceCombatVisualFragment`（转身 + 后续 VAT 动画）。详见
 * `AsceReplicationActionHandlers.h` 与 `技能系统设计.md` §6.1。
 *
 * 基类用**被动通道专用**的 `FAsceLightReplicatedAgentBase`（`TemplateID` 不复制）——见其文件头 + `Networking/Mass复制设计.md`。
 */
USTRUCT()
struct FAsceActionReplicatedAgent : public FAsceLightReplicatedAgentBase
{
	GENERATED_BODY()

	const FReplicatedActionStateData& GetReplicatedActionStateData() const { return ActionState; }
	FReplicatedActionStateData& GetReplicatedActionStateDataMutable() { return ActionState; }

private:
	UPROPERTY(Transient)
	FReplicatedActionStateData ActionState;
};

/** FastArray Item，仿照MassCrowd的FCrowdFastArrayItem */
USTRUCT()
struct FAsceActionFastArrayItem : public FMassFastArrayItemBase
{
	GENERATED_BODY()

	FAsceActionFastArrayItem() = default;
	FAsceActionFastArrayItem(const FAsceActionReplicatedAgent& InAgent, const FMassReplicatedAgentHandle InHandle)
		: FMassFastArrayItemBase(InHandle)
		, Agent(InAgent)
	{}

	typedef FAsceActionReplicatedAgent FReplicatedAgentType;

	UPROPERTY()
	FAsceActionReplicatedAgent Agent;
};

/** 直接继承通用模板即可（**被动bubble**），见AsceReplicationActionHandlers.h类头注释 */
class FAsceActionClientBubbleHandler : public TAsceActionClientBubbleHandler<FAsceActionFastArrayItem>
{
};

/** 每个客户端一份，走FastArraySerializer增量同步 */
USTRUCT()
struct FAsceActionClientBubbleSerializer : public FMassClientBubbleSerializerBase
{
	GENERATED_BODY()

	FAsceActionClientBubbleSerializer()
	{
		Bubble.Initialize(Agents, *this);
	}

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FAsceActionFastArrayItem, FAsceActionClientBubbleSerializer>(Agents, DeltaParams, *this);
	}

public:
	FAsceActionClientBubbleHandler Bubble;

protected:
	UPROPERTY(Transient)
	TArray<FAsceActionFastArrayItem> Agents;
};

template<>
struct TStructOpsTypeTraits<FAsceActionClientBubbleSerializer> : public TStructOpsTypeTraitsBase2<FAsceActionClientBubbleSerializer>
{
	enum
	{
		WithNetDeltaSerializer = true,
		WithCopy = false,
	};
};

/** 每个PlayerController一份，负责真正的Actor级网络复制（Owner=对应的PlayerController） */
UCLASS()
class AAsceActionClientBubbleInfo : public AMassClientBubbleInfoBase
{
	GENERATED_BODY()

public:
	AAsceActionClientBubbleInfo(const FObjectInitializer& ObjectInitializer);

	FAsceActionClientBubbleSerializer& GetAsceSerializer() { return AsceSerializer; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Replicated, Transient)
	FAsceActionClientBubbleSerializer AsceSerializer;
};
