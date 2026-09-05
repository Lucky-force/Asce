// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GameFramework/CheatManager.h"
#include "AsceCheatManager.generated.h"

class UAsceAbilitySystemComponent;


#ifndef USING_CHEAT_MANAGER
#define USING_CHEAT_MANAGER (0 && !UE_BUILD_SHIPPING)
#endif // #ifndef USING_CHEAT_MANAGER

/**
 * 
 */
UCLASS()
class ASCEGAME_API UAsceCheatManager : public UObject
{
	GENERATED_BODY()
};
