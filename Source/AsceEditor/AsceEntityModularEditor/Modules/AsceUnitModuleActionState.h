// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "AsceEntityModularEditor/AsceUnitModule.h"

#include "AsceUnitModuleActionState.generated.h"

/** 动作状态机（纯逻辑，无参数）：`FAsceActionFragment` + `FAsceCombatVisualFragment` */
USTRUCT()
struct FAsceUnitModuleActionState : public FAsceUnitModule
{
	GENERATED_BODY()

	virtual FName GetModuleId() const override;
	virtual FText GetDisplayName() const override;
	virtual void Contribute(const FAsceUnitModuleBuildContext& Context
		, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const override;
};
