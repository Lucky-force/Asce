// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "AsceEntityModularEditor/AsceUnitModule.h"

#include "AsceUnitModuleEther.generated.h"

/**
 * Ether（标记）：完工时由建筑子系统加 `FEtherBuildingFragment`；
 * 器件类型/端口/内布线在 **Ether 建筑定义**（`FAsceEtherBuildingDef`）里，不在这里。
 */
USTRUCT()
struct FAsceUnitModuleEther : public FAsceUnitModule
{
	GENERATED_BODY()

	virtual FName GetModuleId() const override;
	virtual FText GetDisplayName() const override;
	virtual void Contribute(const FAsceUnitModuleBuildContext& Context
		, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const override;
};
