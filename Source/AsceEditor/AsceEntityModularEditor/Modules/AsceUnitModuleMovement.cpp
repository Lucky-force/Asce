// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/Modules/AsceUnitModuleMovement.h"

#include "MassCommonFragments.h"
#include "MassMovementFragments.h"

#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Fragments/AsceMovementFragments.h"
#include "AsceMass/Data/AsceUnitDef.h"
#include "AsceMass/Fragments/AsceOrderFragments.h"

FName FAsceUnitModuleMovement::GetModuleId() const
{
	return FName(TEXT("Movement"));
}

FText FAsceUnitModuleMovement::GetDisplayName() const
{
	return INVTEXT("移动");
}

void FAsceUnitModuleMovement::WriteDefRow(FAsceRTSUnitDef& Row) const
{
	Row.BaseMoveSpeed = BaseMoveSpeed;
	Row.MoveAnims = MoveAnims;
	Row.RunAnims = RunAnims;
	Row.InjuredMoveAnims = InjuredMoveAnims;
}

void FAsceUnitModuleMovement::Contribute(const FAsceUnitModuleBuildContext& Context
	, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const
{
	AsceUnitModuleUtil::AddFragment<FMassVelocityFragment>(OutFragments);
	AsceUnitModuleUtil::AddFragment<FMassForceFragment>(OutFragments);
	AsceUnitModuleUtil::AddFragment<FMassDesiredMovementFragment>(OutFragments);
	AsceUnitModuleUtil::AddFragment<FAsceMoveOrderFragment>(OutFragments);
	AsceUnitModuleUtil::AddFragment<FAsceMovementSimStateFragment>(OutFragments);
	AsceUnitModuleUtil::AddFragment<FAscePositionCorrectionFragment>(OutFragments);


	// 自带体积（若 Volume 模块已先提供，值以 Volume 为准）
	AsceUnitModuleUtil::AddFragment<FAgentRadiusFragment>(OutFragments);
	AsceUnitModuleUtil::AddFragment<FAsceHeightFragment>(OutFragments);

	OutTags.Add(FMassCodeDrivenMovementTag::StaticStruct());
	OutTags.Add(FAsceMovableUnitTag::StaticStruct());
}

void FAsceUnitModuleMovement::CollectPreview(FAsceUnitModulePreviewActions& Actions) const
{
	int32 VariantIndex = 0;
	for (const TSoftObjectPtr<UAnimSequence>& Animation : MoveAnims)
	{
		if (!Animation.IsNull())
		{
			Actions.AddAnimation(MoveAnims.Num() > 1
				? FString::Printf(TEXT("Move #%d"), ++VariantIndex) : FString(TEXT("Move")), Animation);
		}
	}

	VariantIndex = 0;
	for (const TSoftObjectPtr<UAnimSequence>& Animation : RunAnims)
	{
		if (!Animation.IsNull())
		{
			Actions.AddAnimation(RunAnims.Num() > 1
				? FString::Printf(TEXT("Run #%d"), ++VariantIndex) : FString(TEXT("Run")), Animation);
		}
	}

	VariantIndex = 0;
	for (const TSoftObjectPtr<UAnimSequence>& Animation : InjuredMoveAnims)
	{
		if (!Animation.IsNull())
		{
			Actions.AddAnimation(InjuredMoveAnims.Num() > 1
				? FString::Printf(TEXT("InjuredMove #%d"), ++VariantIndex) : FString(TEXT("InjuredMove")), Animation);
		}
	}
}
