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
	Row.IdleAnim = IdleAnim;
	Row.IdleBreakAnims = IdleBreakAnims;
	Row.DyingAnim = DyingAnim;
	Row.ControlledAnim = ControlledAnim;
	Row.StunnedAnim = StunnedAnim;
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
	if (!IdleAnim.IsNull())
	{
		Actions.AddAnimation(TEXT("Idle"), IdleAnim);
	}

	int32 VariantIndex = 0;
	for (const TSoftObjectPtr<UAnimSequence>& Animation : IdleBreakAnims)
	{
		if (!Animation.IsNull())
		{
			Actions.AddAnimation(IdleBreakAnims.Num() > 1
				? FString::Printf(TEXT("IdleBreak #%d"), ++VariantIndex) : FString(TEXT("IdleBreak")), Animation);
		}
	}

	if (!DyingAnim.IsNull())
	{
		Actions.AddAnimation(TEXT("Dying"), DyingAnim);
	}
	if (!ControlledAnim.IsNull())
	{
		Actions.AddAnimation(TEXT("Controlled"), ControlledAnim);
	}
	if (!StunnedAnim.IsNull())
	{
		Actions.AddAnimation(TEXT("Stunned"), StunnedAnim);
	}
}
