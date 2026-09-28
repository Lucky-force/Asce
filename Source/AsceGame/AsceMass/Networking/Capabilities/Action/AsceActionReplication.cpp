// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceActionReplication.h"

#include "MassExecutionContext.h"
#include "Net/UnrealNetwork.h"

// FAsceActionClientBubbleHandler的PostReplicatedAdd/PostReplicatedChange/
// SetActionStateFromFragment都已经在通用模板TAsceActionClientBubbleHandler里
// 实现了（见AsceReplicationActionHandlers.h），这里不需要再写。

AAsceActionClientBubbleInfo::AAsceActionClientBubbleInfo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Serializers.Add(&AsceSerializer);
}

void AAsceActionClientBubbleInfo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(AAsceActionClientBubbleInfo, AsceSerializer, SharedParams);
}
