// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/Modules/AsceUnitModuleEther.h"

#include "AsceMass/Fragments/AsceCoreFragments.h"

FName FAsceUnitModuleEther::GetModuleId() const
{
	return FName(TEXT("Ether"));
}

FText FAsceUnitModuleEther::GetDisplayName() const
{
	return INVTEXT("Ether 建筑");
}

void FAsceUnitModuleEther::Contribute(const FAsceUnitModuleBuildContext& Context
	, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const
{
	// 只标记"是 Ether 建筑"：注册/端口在完工时由建筑子系统处理
	OutTags.Add(FAsceBuildingTag::StaticStruct());
}
