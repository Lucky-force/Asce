// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/Modules/AsceUnitModuleAnimation.h"

#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Data/AsceUnitDef.h"

FName FAsceUnitModuleAnimation::GetModuleId() const
{
	return FName(TEXT("Animation"));
}

FText FAsceUnitModuleAnimation::GetDisplayName() const
{
	return INVTEXT("动画");
}

void FAsceUnitModuleAnimation::Contribute(const FAsceUnitModuleBuildContext& Context
	, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const
{
	AsceUnitModuleUtil::AddFragment<FAsceAnimPlaybackFragment>(OutFragments);
}

void FAsceUnitModuleAnimation::WriteDefRow(FAsceRTSUnitDef& Row) const
{
	Row.AnimSet = AnimSet;
	Row.IdleAnims = IdleAnims;
	Row.IdleBreakAnims = IdleBreakAnims;
	Row.DyingAnims = DyingAnims;
	Row.ControlledAnims = ControlledAnims;
	Row.StunnedAnims = StunnedAnims;
}

void FAsceUnitModuleAnimation::Validate(TArray<FText>& OutErrors) const
{
	if (AnimSet.IsNull())
	{
		OutErrors.Add(INVTEXT("动画：没有配置 AnimSet"));
	}
}

void FAsceUnitModuleAnimation::CollectPreview(FAsceUnitModulePreviewActions& Actions) const
{
	int32 VariantIndex = 0;
	for (const TSoftObjectPtr<UAnimSequence>& Animation : IdleAnims)
	{
		if (!Animation.IsNull())
		{
			Actions.AddAnimation(IdleAnims.Num() > 1
				? FString::Printf(TEXT("Idle #%d"), ++VariantIndex) : FString(TEXT("Idle")), Animation);
		}
	}

	VariantIndex = 0;
	for (const TSoftObjectPtr<UAnimSequence>& Animation : IdleBreakAnims)
	{
		if (!Animation.IsNull())
		{
			Actions.AddAnimation(IdleBreakAnims.Num() > 1
				? FString::Printf(TEXT("IdleBreak #%d"), ++VariantIndex) : FString(TEXT("IdleBreak")), Animation);
		}
	}

	VariantIndex = 0;
	for (const TSoftObjectPtr<UAnimSequence>& Animation : DyingAnims)
	{
		if (!Animation.IsNull())
		{
			Actions.AddAnimation(DyingAnims.Num() > 1
				? FString::Printf(TEXT("Dying #%d"), ++VariantIndex) : FString(TEXT("Dying")), Animation);
		}
	}
}
