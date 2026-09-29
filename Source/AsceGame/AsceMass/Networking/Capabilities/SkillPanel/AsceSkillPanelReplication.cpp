// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceSkillPanelReplication.h"

#include "Net/UnrealNetwork.h"

// 与 Health/Mana 一样：业务逻辑全在 TAsceValue* 模板里，这里只剩 BubbleInfo Actor 必须手写的两块样板

AAsceSkillPanelClientBubbleInfo::AAsceSkillPanelClientBubbleInfo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Serializers.Add(&AsceSerializer);
}

void AAsceSkillPanelClientBubbleInfo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(AAsceSkillPanelClientBubbleInfo, AsceSerializer, SharedParams);
}
