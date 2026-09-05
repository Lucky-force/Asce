// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine/DataAsset.h"

#include "AscePawnData.generated.h"

#define UE_API ASCEGAME_API

class APawn;
class UAsceAbilitySet;
class UAsceAbilityTagRelationshipMapping;
class UAsceCameraMode;
class UAsceInputConfig;
class UObject;


/**
 * UAscePawnData
 *
 *	Non-mutable data asset that contains properties used to define a pawn.
 */
UCLASS(MinimalAPI, BlueprintType, Const, Meta = (DisplayName = "Asce Pawn Data", ShortTooltip = "Data asset used to define a Pawn."))
class UAscePawnData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	UE_API UAscePawnData(const FObjectInitializer& ObjectInitializer);

public:

	// Class to instantiate for this pawn (should usually derive from AAscePawn or AAsceCharacter).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asce|Pawn")
	TSubclassOf<APawn> PawnClass;

	//Ability sets to grant to this pawn's ability system.
	//授予该Pawn的能力
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asce|Abilities")
	TArray<TObjectPtr<UAsceAbilitySet>> AbilitySets;

	//What mapping of ability tags to use for actions taking by this pawn
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asce|Abilities")
	TObjectPtr<UAsceAbilityTagRelationshipMapping> TagRelationshipMapping;

	//Input configuration used by player controlled pawns to create input mappings and bind input actions.
	//用来创建输入映射和绑定输入行为的输入配置
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asce|Input")
	TObjectPtr<UAsceInputConfig> InputConfig;

	//Default camera mode used by player controlled pawns.
	//这个Pawn的默认相机模式
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asce|Camera")
	TSubclassOf<UAsceCameraMode> DefaultCameraMode;
};

#undef UE_API
