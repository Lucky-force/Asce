// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/Modules/AsceUnitModuleBuilding.h"

#include "AsceMass/Fragments/AsceCoreFragments.h"

FName FAsceUnitModuleBuilding::GetModuleId() const
{
	return FName(TEXT("Building"));
}

FText FAsceUnitModuleBuilding::GetDisplayName() const
{
	return INVTEXT("建筑");
}

void FAsceUnitModuleBuilding::Contribute(const FAsceUnitModuleBuildContext& Context
	, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const
{
	OutTags.Add(FAsceBuildingTag::StaticStruct());
}
