// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "DataTableSystem/AsceRowNameWrapper.h"
#include "AsceEntityModularEditor/AsceUnitModule.h"

#include "AsceUnitModuleCombat.generated.h"

class UAnimSequence;

/** 普攻：`FAsceCombatAttributesFragment`（属性，走值复制）+ `FAsceCombatFragment`（运行时，自带 Action/CombatVisual）；数值烘进 fragment，攻击行写 def */
USTRUCT()
struct FAsceUnitModuleCombat : public FAsceUnitModule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.0"))
	float AttackRange = 300.f;

	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.0"))
	float AttackDamage = 10.f;

	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "1.0"))
	float AttackSpeed = 180.f;

	/** 攻击动画（多个变体，随机/按条件选；预览用，运行时由 cue/树读取后接入） */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TArray<TSoftObjectPtr<UAnimSequence>> AttackAnims;

	/** 攻击动作行（`AsceActionDefRow`，必须 `ReleaseMode == Auto`） */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (DataTableBinding = "AsceActionDefRow:ActionId"))
	FAsceRowNameWrapper AttackAction;

	virtual FName GetModuleId() const override;
	virtual FText GetDisplayName() const override;
	virtual void Contribute(const FAsceUnitModuleBuildContext& Context
		, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const override;
	virtual void CollectPreview(FAsceUnitModulePreviewActions& Actions) const override;
	virtual void WriteDefRow(FAsceRTSUnitDef& Row) const override;
	virtual void Validate(TArray<FText>& OutErrors) const override;
};
