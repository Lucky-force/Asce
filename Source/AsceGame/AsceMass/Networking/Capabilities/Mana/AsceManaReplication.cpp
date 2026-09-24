// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceManaReplication.h"

#include "Net/UnrealNetwork.h"

// 与 Health 一样：业务逻辑全在 TAsceScalar* 模板里，这里只剩 BubbleInfo Actor 必须手写的两块样板

AAsceManaClientBubbleInfo::AAsceManaClientBubbleInfo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Serializers.Add(&AsceSerializer);
}

void AAsceManaClientBubbleInfo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(AAsceManaClientBubbleInfo, AsceSerializer, SharedParams);
}
