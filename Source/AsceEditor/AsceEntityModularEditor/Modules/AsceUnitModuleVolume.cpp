// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/Modules/AsceUnitModuleVolume.h"

#include "MassCommonFragments.h"

#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Data/AsceUnitDef.h"

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

void FAsceUnitModuleVolume::WriteDefRow(FAsceRTSUnitDef& Row) const
{
	Row.Radius = Radius;
	Row.Height = Height;
}

void FAsceUnitModuleVolume::CollectPreview(FAsceUnitModulePreviewActions& Actions) const
{
	// DrawWireCapsule 的 HalfHeight 是“总高度的一半”（它内部再扣掉半径画圆柱段），
	// 不是圆柱段的一半；之前传 Height/2 - Radius 会把胶囊画成退化的小点。
	Actions.AddCapsule(Radius, Height * 0.5f, FLinearColor(1.f, 0.85f, 0.2f));
}
