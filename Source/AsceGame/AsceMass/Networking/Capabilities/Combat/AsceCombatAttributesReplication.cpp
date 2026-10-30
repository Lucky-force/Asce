// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceCombatAttributesReplication.h"

#include "Net/UnrealNetwork.h"

// 逻辑全在通用模板里（Core/AsceValueReplicationBubbleHandler.h），
// 这里只有 BubbleInfo Actor 的三件套：构造函数注册 Serializer、Replicated 属性、GetLifetimeReplicatedProps。

AAsceCombatAttributesClientBubbleInfo::AAsceCombatAttributesClientBubbleInfo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Serializers.Add(&AsceSerializer);
}

void AAsceCombatAttributesClientBubbleInfo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(AAsceCombatAttributesClientBubbleInfo, AsceSerializer, SharedParams);
}
