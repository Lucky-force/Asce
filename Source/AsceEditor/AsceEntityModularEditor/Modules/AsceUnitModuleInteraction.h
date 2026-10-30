// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "AsceEntityModularEditor/AsceUnitModule.h"

#include "AsceUnitModuleInteraction.generated.h"

/** 可交互：`FAsceInteractableFragment` + tag；自带 Radius（值优先用 Volume 的） */
USTRUCT()
struct FAsceUnitModuleInteraction : public FAsceUnitModule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Interaction", meta = (ClampMin = "0.0"))
	float InteractionRange = 150.f;

	UPROPERTY(EditAnywhere, Category = "Interaction")
	bool bPlayerOnlyInteraction = false;

	virtual FName GetModuleId() const override;
	virtual FText GetDisplayName() const override;
	virtual void Contribute(const FAsceUnitModuleBuildContext& Context
		, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const override;
	virtual void CollectPreview(FAsceUnitModulePreviewActions& Actions) const override;
};
