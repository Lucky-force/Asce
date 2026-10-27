// Fill out your copyright notice in the Description page of Project Settings.

#include "Cheat/Actions/RTS/AsceRTSCheatArgTypes.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include "AsceMass/Building/AsceBuildingDef.h"
#include "AsceMass/Combat/AsceCombatDef.h"
#include "AsceMass/Data/AsceEntityDefTypes.h"
#include "AsceMass/Data/AsceUnitDef.h"
#include "DataTableSystem/AsceDataTableSubsystem.h"
#include "Player/AscePlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRTSCheatArgTypes)

// ---------------------------------------------------------------------------------------------
// 候选列表：实体行 / 动作行 / 阵营
// ---------------------------------------------------------------------------------------------

TArray<FString> FAsceCheatArgDefRow::GetSuggestions(const UWorld* World) const
{
	TArray<FString> Result;
	TMap<FName, FAsceEntityDefRow> Rows;
	if (!UAsceDataTableSubsystem::GetDefinitionMap<FAsceEntityDefRow>(Rows))
	{
		return Result;
	}
	for (const TPair<FName, FAsceEntityDefRow>& Pair : Rows)
	{
		const UScriptStruct* DefStruct = Pair.Value.Def.GetScriptStruct();
		if (!DefStruct)
		{
			continue;
		}
		const bool bIsBuilding = DefStruct->IsChildOf(FAsceBuildingDefBase::StaticStruct());
		const bool bIsUnit = DefStruct->IsChildOf(FAsceRTSUnitDef::StaticStruct());
		if ((bUnitsOnly && !bIsUnit) || (bBuildingsOnly && !bIsBuilding))
		{
			continue;   // 只要单位行 / 只要建筑行
		}
		if (!bUnitsOnly && !bBuildingsOnly && !bIsUnit && !bIsBuilding)
		{
			continue;   // 无过滤时只列"可生成的实体"（单位/建筑）
		}
		Result.Add(Pair.Key.ToString());
	}
	Result.Sort([](const FString& A, const FString& B) { return FCString::Atoi(*A) < FCString::Atoi(*B); });
	return Result;
}

TArray<FString> FAsceCheatArgActionRow::GetSuggestions(const UWorld* World) const
{
	TArray<FString> Result;
	TMap<FName, FAsceActionDef> Rows;
	if (!UAsceDataTableSubsystem::GetDefinitionMap<FAsceActionDef>(Rows))
	{
		return Result;
	}
	for (const TPair<FName, FAsceActionDef>& Pair : Rows)
	{
		Result.Add(Pair.Key.ToString());
	}
	Result.Sort();
	return Result;
}

TArray<FString> FAsceCheatArgTeam::GetSuggestions(const UWorld* World) const
{
	// -1 = 用请求者默认，0 = 中立；列表里 AI 阵营可能不在这里，所以允许手输
	TArray<FString> Result;
	Result.Add(TEXT("-1"));
	Result.Add(TEXT("0"));
	if (!World)
	{
		return Result;
	}
	TSet<int32> Seen;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const AAscePlayerState* PlayerState = PC ? PC->GetPlayerState<AAscePlayerState>() : nullptr;
		const int32 TeamId = PlayerState ? PlayerState->GetTeamId() : 0;
		if (TeamId > 0 && !Seen.Contains(TeamId))
		{
			Seen.Add(TeamId);
			Result.Add(FString::FromInt(TeamId));
		}
	}
	return Result;
}
