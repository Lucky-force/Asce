// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/Modules/AsceUnitModuleVolume.h"

#include "MassCommonFragments.h"

#include "AsceMass/Fragments/AsceCoreFragments.h"

FName FAsceUnitModuleVolume::GetModuleId() const
{
	return FName(TEXT("Volume"));
}

FText FAsceUnitModuleVolume::GetDisplayName() const
{
	return INVTEXT("体积");
}

void FAsceUnitModuleVolume::Contribute(const FAsceUnitModuleBuildContext& Context
	, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const
{
	AsceUnitModuleUtil::AddFragment<FAgentRadiusFragment>(OutFragments).Radius = Radius;
	AsceUnitModuleUtil::AddFragment<FAsceHeightFragment>(OutFragments).Height = Height;
}

void FAsceUnitModuleVolume::CollectPreview(FAsceUnitModulePreviewActions& Actions) const
{
	Actions.AddCapsule(Radius, FMath::Max(Height * 0.5f - Radius, 0.f), FLinearColor(1.f, 0.85f, 0.2f));
}
