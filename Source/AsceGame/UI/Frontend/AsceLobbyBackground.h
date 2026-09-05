// Fill out your copyright notice in the Description page of Project Settings.


#pragma once

#include "Engine/DataAsset.h"

#include "AsceLobbyBackground.generated.h"

class UObject;
class UWorld;

/**
 * Developer settings / editor cheats
 * 要使用的作为大厅背景的关卡
 */
UCLASS(config=EditorPerProjectUserSettings, MinimalAPI)
class UAsceLobbyBackground : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UWorld> BackgroundLevel;
};
