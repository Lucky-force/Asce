// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "AsceEntityModularEditor/AsceUnitModule.h"

#include "AsceUnitModuleVision.generated.h"

/** 视野：揭示雾；自带 Height（眼高优先取 Volume 的 Height） */
USTRUCT()
struct FAsceUnitModuleVision : public FAsceUnitModule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Vision", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 SightRadius = 20;

	virtual FName GetModuleId() const override;
	virtual FText GetDisplayName() const override;
	virtual void Contribute(const FAsceUnitModuleBuildContext& Context
		, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const override;
	virtual void WriteDefRow(FAsceRTSUnitDef& Row) const override;
	virtual void CollectPreview(FAsceUnitModulePreviewActions& Actions) const override;
};
