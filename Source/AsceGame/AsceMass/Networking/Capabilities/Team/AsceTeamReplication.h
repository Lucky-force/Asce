// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "MassClientBubbleInfoBase.h"
#include "AsceMass/Networking/Core/AsceReplicationValueHandlers.h"
#include "AsceMass/Networking/Core/AsceValueReplicationBubbleHandler.h"
#include "AsceMass/Networking/Core/AsceLightReplicatedAgent.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceTeamReplication.generated.h"

class AAsceTeamClientBubbleInfo;

/**
 * "阵营同步"能力：把服务器权威的 **TeamId** 复制给客户端实体。
 *
 * 为什么需要它：`FAsceTeamFragment` 不会被实体复制自动带上（客户端模板默认 TeamKey=0），
 * 而客户端的空间列/迷雾/选择/UI 都按它过滤 ⇒ 没有这条通道，客户端所有单位会被当成 0 队。
 *
 * 载荷 = `FReplicatedValueUInt16`（TeamId 是 16 位；通道按基本类型泛化，
 * 换位宽只需换一个载荷结构，见 `Core/AsceReplicationValueHandlers.h`）。
 * 客户端写回时只写裸 TeamId（高 16 位阵营下标留 0）——所有读路径都会用**本端**阵营表
 * （`AAsceGameState::FindTeamIndex/GetTeamKey`）按 TeamId 重新映射，不依赖写入方的 index。
 */
USTRUCT()
struct FAsceTeamReplicatedAgent : public FAsceLightReplicatedAgentBase
{
	GENERATED_BODY()

	const FReplicatedValueUInt16& GetTeamData() const { return TeamId; }
	FReplicatedValueUInt16& GetTeamDataMutable() { return TeamId; }

private:
	UPROPERTY(Transient)
	FReplicatedValueUInt16 TeamId;
};

/** FastArray Item（同 Health） */
USTRUCT()
struct FAsceTeamFastArrayItem : public FMassFastArrayItemBase
{
	GENERATED_BODY()

	FAsceTeamFastArrayItem() = default;
	FAsceTeamFastArrayItem(const FAsceTeamReplicatedAgent& InAgent, const FMassReplicatedAgentHandle InHandle)
		: FMassFastArrayItemBase(InHandle)
		, Agent(InAgent)
	{}

	typedef FAsceTeamReplicatedAgent FReplicatedAgentType;

	UPROPERTY()
	FAsceTeamReplicatedAgent Agent;
};

/** 阵营同步的业务逻辑（唯一手写处）：Fragment <-> TeamId */
struct FAsceTeamPolicy
{
	typedef FAsceTeamFragment FragmentType;

	using ValueType = uint16;
	using FValueData = FReplicatedValueUInt16;
	using FAgentType = FAsceTeamReplicatedAgent;
	using FFastArrayItemType = FAsceTeamFastArrayItem;
	using FBubbleInfoType = AAsceTeamClientBubbleInfo;

	/** 只有一个字段（TeamId） */
	static constexpr int32 NumFields = 1;

	static FValueData& GetMutableData(FAsceTeamReplicatedAgent& Agent, const int32 FieldIndex)
	{
		return Agent.GetTeamDataMutable();
	}

	static const FValueData& GetData(const FAsceTeamReplicatedAgent& Agent, const int32 FieldIndex)
	{
		return Agent.GetTeamData();
	}

	static ValueType ExtractFromFragment(const FAsceTeamFragment& Fragment, const int32 FieldIndex)
	{
		// 只复制跨端一致的 TeamId（低 16 位）；本端下标（高 16 位）不复制
		return static_cast<ValueType>(AsceTeamId(Fragment.TeamKey));
	}

	static void ApplyToFragment(FAsceTeamFragment& Fragment, const int32 FieldIndex, const ValueType Value)
	{
		// 客户端只写裸 TeamId（下标位留 0）：读路径会用本端阵营表按 TeamId 重映射
		Fragment.TeamKey = static_cast<int32>(Value);
	}

	static bool ValuesDiffer(const ValueType NewValue, const ValueType OldValue, const int32 FieldIndex)
	{
		return NewValue != OldValue;
	}
};

using FAsceTeamClientBubbleHandler = TAsceValueClientBubbleHandler<FAsceTeamFastArrayItem, FAsceTeamPolicy>;

/**
 * 每个客户端一份，走 FastArraySerializer 增量同步。
 * **必须**声明 traits，否则引擎不会调 `NetDeltaSerialize`（同 Health，见其注释）。
 */
USTRUCT()
struct FAsceTeamClientBubbleSerializer : public FMassClientBubbleSerializerBase
{
	GENERATED_BODY()

	FAsceTeamClientBubbleSerializer()
	{
		Bubble.Initialize(Agents, *this);
	}

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FAsceTeamFastArrayItem, FAsceTeamClientBubbleSerializer>(Agents, DeltaParams, *this);
	}

public:
	FAsceTeamClientBubbleHandler Bubble;

protected:
	UPROPERTY(Transient)
	TArray<FAsceTeamFastArrayItem> Agents;
};

template<>
struct TStructOpsTypeTraits<FAsceTeamClientBubbleSerializer> : public TStructOpsTypeTraitsBase2<FAsceTeamClientBubbleSerializer>
{
	enum
	{
		WithNetDeltaSerializer = true,
		WithCopy = false,
	};
};

/** 客户端 BubbleInfo Actor：约定 = 提供 `GetAsceSerializer()`（内含 public `Bubble`） */
UCLASS()
class AAsceTeamClientBubbleInfo : public AMassClientBubbleInfoBase
{
	GENERATED_BODY()

public:
	AAsceTeamClientBubbleInfo(const FObjectInitializer& ObjectInitializer);

	FAsceTeamClientBubbleSerializer& GetAsceSerializer() { return AsceSerializer; }
	const FAsceTeamClientBubbleSerializer& GetAsceSerializer() const { return AsceSerializer; }

protected:
	UPROPERTY(Transient, Replicated)
	FAsceTeamClientBubbleSerializer AsceSerializer;
};
