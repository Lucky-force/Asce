// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GameFeaturesProjectPolicies.h"

#include "AsceGameFeaturePolicy.generated.h"

#define UE_API ASCEGAME_API

class FName;
class UGameFeatureData;
struct FPrimaryAssetId;

/**
 * GameFeature 插件策略（移植自 Lyra 的 ULyraGameFeaturePolicy）。
 * PrimaryAsset 注册/预载等仍走 UDefaultGameFeaturesProjectPolicies，这里只补：
 *  - 强类型 Policy 访问入口 Get()
 *  - 加载模式（编辑器两端数据都加载，避免插件 bundle 预载卡顿）
 */
UCLASS(MinimalAPI, Config = Game)
class UAsceGameFeaturePolicy : public UDefaultGameFeaturesProjectPolicies
{
	GENERATED_BODY()

public:
	UE_API static UAsceGameFeaturePolicy& Get();

	UE_API UAsceGameFeaturePolicy(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UGameFeaturesProjectPolicies interface
	UE_API virtual void GetGameFeatureLoadingMode(bool& bLoadClientData, bool& bLoadServerData) const override;
	//~End of UGameFeaturesProjectPolicies interface
};

#undef UE_API
