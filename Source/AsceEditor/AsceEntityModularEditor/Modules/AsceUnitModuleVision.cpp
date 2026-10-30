// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/Modules/AsceUnitModuleVision.h"

#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Vision/AsceVisionDefines.h"
#include "AsceMass/Vision/AsceVisionFragments.h"

#include "AsceEntityModularEditor/Modules/AsceUnitModuleVolume.h"

FName FAsceUnitModuleVision::GetModuleId() const
{
	return FName(TEXT("Vision"));
}

FText FAsceUnitModuleVision::GetDisplayName() const
{
	return INVTEXT("视野");
}

void FAsceUnitModuleVision::Contribute(const FAsceUnitModuleBuildContext& Context
	, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const
{
	FAsceVisionViewFragment& View = AsceUnitModuleUtil::AddFragment<FAsceVisionViewFragment>(OutFragments);
	View.SightRadius = static_cast<uint16>(FMath::Clamp(SightRadius, 1, MAX_uint16));

	// 眼高优先取 Volume 的 Height；没勾 Volume 时用默认 Height（0 = 贴地）
	const FAsceUnitModuleVolume* Volume = Context.Find<FAsceUnitModuleVolume>();
	FAsceHeightFragment& Height = AsceUnitModuleUtil::AddFragment<FAsceHeightFragment>(OutFragments);
	Height.Height = Volume ? Volume->Height : 0.f;
	View.EyeHeightCm = static_cast<uint16>(FMath::Clamp(Height.Height, 0.f, static_cast<float>(MAX_uint16)));

	AsceUnitModuleUtil::AddFragment<FAsceVisionFogStateFragment>(OutFragments);
}

void FAsceUnitModuleVision::CollectPreview(FAsceUnitModulePreviewActions& Actions) const
{
	// 视野半径：格 → cm
	Actions.AddCircle(static_cast<float>(SightRadius) * AsceVision::FlowFieldGridSizeCm, FLinearColor(0.2f, 0.8f, 1.f));
}
