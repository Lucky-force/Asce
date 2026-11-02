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
	Row.MoveAnim = MoveAnim;
	Row.RunAnim = RunAnim;
	Row.InjuredMoveAnim = InjuredMoveAnim;
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
	if (!MoveAnim.IsNull())
	{
		Actions.AddAnimation(TEXT("Move"), MoveAnim);
	}
	if (!RunAnim.IsNull())
	{
		Actions.AddAnimation(TEXT("Run"), RunAnim);
	}
	if (!InjuredMoveAnim.IsNull())
	{
		Actions.AddAnimation(TEXT("InjuredMove"), InjuredMoveAnim);
	}
}
