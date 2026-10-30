// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "AsceEntityModularEditor/AsceUnitModule.h"

#include "AsceUnitModuleBuilding.generated.h"

/**
 * 建筑（标记）：提供 `FAsceBuildingTag`；
 * 尺寸/遮挡/工期/CoreClass 在**建筑定义**（`FAsceBuildingDefBase`）里，不在这里。
 */
USTRUCT()
struct FAsceUnitModuleBuilding : public FAsceUnitModule
{
	GENERATED_BODY()

	virtual FName GetModuleId() const override;
	virtual FText GetDisplayName() const override;
	virtual void Contribute(const FAsceUnitModuleBuildContext& Context
		, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const override;
};
