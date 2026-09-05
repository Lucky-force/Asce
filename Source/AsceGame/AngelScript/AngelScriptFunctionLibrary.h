// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AngelScriptFunctionLibrary.generated.h"

DECLARE_DYNAMIC_DELEGATE(FCommonCallbackDelegate);

/**
 * 
 */
UCLASS()
class ASCEGAME_API UAngelScriptFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
	UFUNCTION(BlueprintCallable)
	static void DelayCall(float DelayTime, UObject* ContextObject, FCommonCallbackDelegate Callback, FTimerHandle& TimerHandle);
	
	//Math
	UFUNCTION(BlueprintCallable)
	static FRotator MakeRotationFromAxes(FVector Forward, FVector Right, FVector Up);
	
	UFUNCTION(BlueprintCallable)
	static FVector TransformDirection(const FTransform& T, FVector Direction);
};
