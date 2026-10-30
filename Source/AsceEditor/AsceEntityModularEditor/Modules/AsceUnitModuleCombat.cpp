// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/Modules/AsceUnitModuleCombat.h"

#include "AsceMass/Combat/AsceActionDefLibrary.h"
#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Data/AsceUnitDef.h"

namespace
{
	/** 行名 → `FAsceActionDef`（找不到返回 false） */
	bool LookupActionDef(const FName RowName, FAsceActionDef& OutDef)
	{
		OutDef = FAsceActionDef();
		return !RowName.IsNone() && FAsceActionDefLibrary::GetActionDefByRowName(RowName, OutDef);
	}
}

FName FAsceUnitModuleCombat::GetModuleId() const
{
	return FName(TEXT("Combat"));
}

FText FAsceUnitModuleCombat::GetDisplayName() const
{
	return INVTEXT("普攻");
}

void FAsceUnitModuleCombat::Contribute(const FAsceUnitModuleBuildContext& Context
	, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const
{
	FAsceCombatAttributesFragment& Combat = AsceUnitModuleUtil::AddFragment<FAsceCombatAttributesFragment>(OutFragments);
	Combat.AttackRange = AttackRange;
	Combat.AttackDamage = AttackDamage;
	Combat.AttackSpeed = AttackSpeed;

	FAsceActionDef AttackDef;
	if (LookupActionDef(AttackAction.RowName, AttackDef))
	{
		Combat.AttackActionId = AttackDef.ActionId;
		Combat.RangeKind = AttackDef.RangeKind;
	}

	// 运行时状态（指定目标 / 扫描节流）跟属性分开
	AsceUnitModuleUtil::AddFragment<FAsceCombatFragment>(OutFragments);

	// 自带动作态（若 ActionState 模块已先提供，这两项会被去重跳过）
	AsceUnitModuleUtil::AddFragment<FAsceActionFragment>(OutFragments);
	AsceUnitModuleUtil::AddFragment<FAsceCombatVisualFragment>(OutFragments);
}

void FAsceUnitModuleCombat::WriteDefRow(FAsceRTSUnitDef& Row) const
{
	Row.AttackAction = AttackAction;
	Row.AttackAnims = AttackAnims;
}

void FAsceUnitModuleCombat::Validate(TArray<FText>& OutErrors) const
{
	if (AttackAction.RowName.IsNone())
	{
		return;
	}

	FAsceActionDef Def;
	if (!LookupActionDef(AttackAction.RowName, Def))
	{
		OutErrors.Add(FText::FromString(FString::Printf(TEXT("普攻：动作行 [%s] 不存在"), *AttackAction.RowName.ToString())));
		return;
	}
	if (Def.ReleaseMode != EAsceReleaseMode::Auto)
	{
		OutErrors.Add(FText::FromString(FString::Printf(TEXT("普攻：动作行 [%s] 的 ReleaseMode 应为 Auto"), *AttackAction.RowName.ToString())));
	}
}

void FAsceUnitModuleCombat::CollectPreview(FAsceUnitModulePreviewActions& Actions) const
{
	Actions.AddCircle(AttackRange, FLinearColor(1.f, 0.25f, 0.2f));

	int32 VariantIndex = 0;
	for (const TSoftObjectPtr<UAnimSequence>& Animation : AttackAnims)
	{
		if (!Animation.IsNull())
		{
			Actions.AddAnimation(AttackAnims.Num() > 1
				? FString::Printf(TEXT("Attack #%d"), ++VariantIndex)
				: FString(TEXT("Attack")), Animation);
		}
	}
}
