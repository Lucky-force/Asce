// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceHealthReplication.h"

#include "Net/UnrealNetwork.h"

// PostReplicatedAdd/PostReplicatedChange/SetScalarFromFragment现在全部收在
// TAsceScalarClientBubbleHandler模板里了（见Core/AsceScalarReplicationBubbleHandler.h），
// 这个.cpp只剩下BubbleInfo Actor本身必须手写的两块样板（构造函数注册Serializer、声明Replicated属性）。

AAsceHealthClientBubbleInfo::AAsceHealthClientBubbleInfo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Serializers.Add(&AsceSerializer);
}

void AAsceHealthClientBubbleInfo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(AAsceHealthClientBubbleInfo, AsceSerializer, SharedParams);
}
