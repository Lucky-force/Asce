// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceGameFeaturePolicy.h"

#include "GameFeaturesSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceGameFeaturePolicy)

UAsceGameFeaturePolicy::UAsceGameFeaturePolicy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UAsceGameFeaturePolicy& UAsceGameFeaturePolicy::Get()
{
	return UGameFeaturesSubsystem::Get().GetPolicy<UAsceGameFeaturePolicy>();
}

void UAsceGameFeaturePolicy::GetGameFeatureLoadingMode(bool& bLoadClientData, bool& bLoadServerData) const
{
	// 编辑器里两端数据都加载（同 Lyra）；专用服务器/纯客户端只加载各自那份，避免预载无关 bundle
	bLoadClientData = !IsRunningDedicatedServer();
	bLoadServerData = !IsRunningClientOnly();
}
