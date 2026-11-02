// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/Modules/AsceUnitModuleSkills.h"

#include "AsceMass/Combat/AsceActionDefLibrary.h"
#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Data/AsceUnitDef.h"

namespace
{
	bool LookupSkillActionDef(const FName RowName, FAsceActionDef& OutDef)
	{
		OutDef = FAsceActionDef();
		return !RowName.IsNone() && FAsceActionDefLibrary::GetActionDefByRowName(RowName, OutDef);
	}
}

FName FAsceUnitModuleSkills::GetModuleId() const
{
	return FName(TEXT("Skills"));
}

FText FAsceUnitModuleSkills::GetDisplayName() const
{
	return INVTEXT("技能");
}

void FAsceUnitModuleSkills::Contribute(const FAsceUnitModuleBuildContext& Context
	, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const
{
	FAsceSkillSlotsFragment& Slots = AsceUnitModuleUtil::AddFragment<FAsceSkillSlotsFragment>(OutFragments);
	for (int32 Index = 0; Index < FAsceSkillSlotsFragment::NumSkillSlots; ++Index)
	{
		FAsceActionDef SkillDef;
		Slots.Slots[Index].ActionId = LookupSkillActionDef(SkillSlots[Index].RowName, SkillDef) ? SkillDef.ActionId : 0;
	}

	FAsceManaFragment& Mana = AsceUnitModuleUtil::AddFragment<FAsceManaFragment>(OutFragments);
	Mana.MaxMana = MaxMana;
	Mana.CurrentMana = MaxMana;

	// 自带动作态（若 ActionState 模块已先提供，这两项会被去重跳过）
	AsceUnitModuleUtil::AddFragment<FAsceActionFragment>(OutFragments);
	AsceUnitModuleUtil::AddFragment<FAsceCombatVisualFragment>(OutFragments);
}

void FAsceUnitModuleSkills::WriteDefRow(FAsceRTSUnitDef& Row) const
{
	Row.MaxMana = MaxMana;
	for (int32 Index = 0; Index < FAsceSkillSlotsFragment::NumSkillSlots; ++Index)
	{
		Row.SkillSlots[Index] = SkillSlots[Index];
	}
}

void FAsceUnitModuleSkills::Validate(TArray<FText>& OutErrors) const
{
	for (int32 Index = 0; Index < FAsceSkillSlotsFragment::NumSkillSlots; ++Index)
	{
		const FName RowName = SkillSlots[Index].RowName;
		if (RowName.IsNone())
		{
			continue;
		}
		FAsceActionDef Def;
		if (!LookupSkillActionDef(RowName, Def))
		{
			OutErrors.Add(FText::FromString(FString::Printf(TEXT("技能槽 %d：动作行 [%s] 不存在"), Index + 1, *RowName.ToString())));
			continue;
		}
		if (Def.ReleaseMode != EAsceReleaseMode::Manual)
		{
			OutErrors.Add(FText::FromString(FString::Printf(TEXT("技能槽 %d：动作行 [%s] 的 ReleaseMode 应为 Manual"), Index + 1, *RowName.ToString())));
		}
	}
}
