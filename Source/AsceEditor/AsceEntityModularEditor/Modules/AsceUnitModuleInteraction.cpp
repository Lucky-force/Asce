// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/Modules/AsceUnitModuleInteraction.h"

#include "MassCommonFragments.h"

#include "AsceMass/Fragments/AsceInteractionFragments.h"

FName FAsceUnitModuleInteraction::GetModuleId() const
{
	return FName(TEXT("Interaction"));
}

FText FAsceUnitModuleInteraction::GetDisplayName() const
{
	return INVTEXT("可交互");
}

void FAsceUnitModuleInteraction::Contribute(const FAsceUnitModuleBuildContext& Context
	, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const
{
	AsceUnitModuleUtil::AddFragment<FAsceInteractableFragment>(OutFragments).InteractionRange = InteractionRange;

	// 自带体积（若 Volume 模块已先提供，值以 Volume 为准）
	AsceUnitModuleUtil::AddFragment<FAgentRadiusFragment>(OutFragments);

	OutTags.Add(bPlayerOnlyInteraction
		? FAscePlayerOnlyInteractableTag::StaticStruct()
		: FAsceGenericInteractableTag::StaticStruct());
}

void FAsceUnitModuleInteraction::CollectPreview(FAsceUnitModulePreviewActions& Actions) const
{
	Actions.AddCircle(InteractionRange, FLinearColor(0.4f, 1.f, 0.5f));
}
