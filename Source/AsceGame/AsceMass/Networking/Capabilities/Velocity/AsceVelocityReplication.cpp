// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceVelocityReplication.h"

#include "MassExecutionContext.h"
#include "Net/UnrealNetwork.h"

// FAsceVelocityClientBubbleHandler的PostReplicatedAdd/PostReplicatedChange/
// SetVelocityFromFragment都已经在通用模板TAsceVelocityClientBubbleHandler里
// 实现了（见AsceReplicationVelocityHandlers.h），这里不需要再写。

AAsceVelocityClientBubbleInfo::AAsceVelocityClientBubbleInfo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Serializers.Add(&AsceSerializer);
}

void AAsceVelocityClientBubbleInfo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(AAsceVelocityClientBubbleInfo, AsceSerializer, SharedParams);
}
