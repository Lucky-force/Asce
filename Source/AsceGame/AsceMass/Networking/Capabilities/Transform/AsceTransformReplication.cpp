// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceTransformReplication.h"

#include "MassExecutionContext.h"
#include "Net/UnrealNetwork.h"

// FAsceTransformClientBubbleHandler的PostReplicatedAdd/PostReplicatedChange/
// SetPositionFromFragment都已经在通用模板TAscePositionClientBubbleHandler里
// 实现了（见AsceReplicationPositionHandlers.h），这里不需要再写。

AAsceTransformClientBubbleInfo::AAsceTransformClientBubbleInfo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Serializers.Add(&AsceSerializer);
}

void AAsceTransformClientBubbleInfo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(AAsceTransformClientBubbleInfo, AsceSerializer, SharedParams);
}
