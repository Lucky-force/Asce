// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "AsceEntityModularEditor/AsceUnitModule.h"

#include "AsceUnitModuleVolume.generated.h"

/** 体积：`FAgentRadiusFragment` + `FAsceHeightFragment`（Radius/Height 的"拥有者"） */
USTRUCT()
struct FAsceUnitModuleVolume : public FAsceUnitModule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Volume", meta = (ClampMin = "1.0"))
	float Radius = 75.f;

	UPROPERTY(EditAnywhere, Category = "Volume", meta = (ClampMin = "1.0"))
	float Height = 150.f;

	virtual FName GetModuleId() const override;
	virtual FText GetDisplayName() const override;
	virtual void Contribute(const FAsceUnitModuleBuildContext& Context
		, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const override;
	virtual void WriteDefRow(FAsceRTSUnitDef& Row) const override;
	virtual void CollectPreview(FAsceUnitModulePreviewActions& Actions) const override;
};
