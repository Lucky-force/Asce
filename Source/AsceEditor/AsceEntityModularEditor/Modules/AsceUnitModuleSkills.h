// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "DataTableSystem/AsceRowNameWrapper.h"
#include "AsceEntityModularEditor/AsceUnitModule.h"

#include "AsceUnitModuleSkills.generated.h"

/** 技能：槽位/CD + 蓝量（自带 Action/CombatVisual）；槽位行写 def */
USTRUCT()
struct FAsceUnitModuleSkills : public FAsceUnitModule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Skills", meta = (ClampMin = "0.0"))
	float MaxMana = 100.f;

	/** 初始技能槽（固定 3 个；行必须 `ReleaseMode == Manual`） */
	UPROPERTY(EditAnywhere, Category = "Skills", meta = (DataTableBinding = "AsceActionDefRow:ActionId"))
	FAsceRowNameWrapper SkillSlots[3];

	virtual FName GetModuleId() const override;
	virtual FText GetDisplayName() const override;
	virtual void Contribute(const FAsceUnitModuleBuildContext& Context
		, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const override;
	virtual void WriteDefRow(FAsceRTSUnitDef& Row) const override;
	virtual void Validate(TArray<FText>& OutErrors) const override;
};
