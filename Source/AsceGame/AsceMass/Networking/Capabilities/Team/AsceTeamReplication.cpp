// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceTeamReplication.h"

#include "Net/UnrealNetwork.h"

// 业务逻辑全在通用模板里（Core/AsceValueReplicationBubbleHandler.h），
// 这里只剩 BubbleInfo Actor 必须手写的两块样板（同 Health）。

AAsceTeamClientBubbleInfo::AAsceTeamClientBubbleInfo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Serializers.Add(&AsceSerializer);
}

void AAsceTeamClientBubbleInfo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(AAsceTeamClientBubbleInfo, AsceSerializer, SharedParams);
}
