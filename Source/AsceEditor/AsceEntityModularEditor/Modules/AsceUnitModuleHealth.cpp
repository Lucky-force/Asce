// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/Modules/AsceUnitModuleHealth.h"

#include "AsceMass/Fragments/AsceCoreFragments.h"

FName FAsceUnitModuleHealth::GetModuleId() const
{
	return FName(TEXT("Health"));
}

FText FAsceUnitModuleHealth::GetDisplayName() const
{
	return INVTEXT("生命值");
}

void FAsceUnitModuleHealth::Contribute(const FAsceUnitModuleBuildContext& Context
	, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const
{
	FAsceHealthFragment& Health = AsceUnitModuleUtil::AddFragment<FAsceHealthFragment>(OutFragments);
	Health.MaxHealth = MaxHealth;
	Health.CurrentHealth = MaxHealth;
}
