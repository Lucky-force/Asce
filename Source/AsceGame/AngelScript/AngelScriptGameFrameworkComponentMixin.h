// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Components/GameFrameworkComponentDelegates.h"
#include "AngelScriptGameFrameworkComponentMixin.generated.h"

class UGameFrameworkComponent;
/**
 * 为了调用c++接口而实现mixin
 */
UCLASS(meta = (ScriptMixin = "UGameFrameworkComponent"))
class ASCEGAME_API UAngelScriptGameFrameworkComponentMixin : public UObject
{
	GENERATED_BODY()
	
public:
	
	UFUNCTION(ScriptCallable)
	static void RegisterAndCallForInitStateChange(UGameFrameworkComponent* Component, FGameplayTag RequiredState, FActorInitStateChangedBPDelegate Delegate, bool bCallImmediately);
	
	UFUNCTION(ScriptCallable)
	static void UnregisterInitStateDelegate(UGameFrameworkComponent* Component, FActorInitStateChangedBPDelegate Delegate);
};
