// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/Modules/AsceUnitModuleActionState.h"

#include "AsceMass/Combat/AsceCombatFragments.h"

FName FAsceUnitModuleActionState::GetModuleId() const
{
	return FName(TEXT("ActionState"));
}

FText FAsceUnitModuleActionState::GetDisplayName() const
{
	return INVTEXT("动作状态机");
}

void FAsceUnitModuleActionState::Contribute(const FAsceUnitModuleBuildContext& Context
	, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const
{
	AsceUnitModuleUtil::AddFragment<FAsceActionFragment>(OutFragments);
	AsceUnitModuleUtil::AddFragment<FAsceCombatVisualFragment>(OutFragments);
}
