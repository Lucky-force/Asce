// Fill out your copyright notice in the Description page of Project Settings.

#pragma once


#include "Engine/DataAsset.h"

#include "AsceGameData.generated.h"

#define UE_API ASCEGAME_API

class UGameplayEffect;
class UObject;

/**
 * UAsceGameData
 *
 *	Non-mutable data asset that contains global game data.
 *	
 *	存放全局游戏数据，不可改变
 */
UCLASS(MinimalAPI, BlueprintType, Const, Meta = (DisplayName = "Asce Game Data", ShortTooltip = "Data asset containing global game data."))
class UAsceGameData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	UE_API UAsceGameData();

	// Returns the loaded game data.
	// 获取存放在AssetManager中的当前加载的GameData
	static UE_API const UAsceGameData& Get();

public:

	// Gameplay effect used to apply damage.  Uses SetByCaller for the damage magnitude.
	// 应用伤害的GE
	UPROPERTY(EditDefaultsOnly, Category = "Default Gameplay Effects", meta = (DisplayName = "Damage Gameplay Effect (SetByCaller)"))
	TSoftClassPtr<UGameplayEffect> DamageGameplayEffect_SetByCaller;

	// Gameplay effect used to apply healing.  Uses SetByCaller for the healing magnitude.
	// 应用治疗的GE
	UPROPERTY(EditDefaultsOnly, Category = "Default Gameplay Effects", meta = (DisplayName = "Heal Gameplay Effect (SetByCaller)"))
	TSoftClassPtr<UGameplayEffect> HealGameplayEffect_SetByCaller;

	// Gameplay effect used to add and remove dynamic tags.
	// 用来添加或删除动态tag的GE
	UPROPERTY(EditDefaultsOnly, Category = "Default Gameplay Effects")
	TSoftClassPtr<UGameplayEffect> DynamicTagGameplayEffect;
};

#undef UE_API
