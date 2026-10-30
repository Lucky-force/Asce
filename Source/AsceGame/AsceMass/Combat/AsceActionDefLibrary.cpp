// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceActionDefLibrary.h"

#include "DataTableSystem/AsceDataTableSubsystem.h"
#include "GameModes/AsceGameplaySettings.h"
#include "StateTree.h"

namespace
{
	/** 行名 → 行 */
	TMap<FName, FAsceActionDef> GActionDefRows;
	/** `ActionId` → 行（`ActionId <= 0` 不入索引：0 = 内置攻击，没有行） */
	TMap<int32, FAsceActionDef> GActionDefsById;
	bool GBuilt = false;

	void EnsureBuilt()
	{
		if (GBuilt)
		{
			return;
		}
		GBuilt = true;   // 先置位：表没登记时也不用每次重试（`InvalidateCache` 可强制重建）
		TMap<FName, FAsceActionDef> RowMap;
		if (!UAsceDataTableSubsystem::GetDefinitionMap<FAsceActionDef>(RowMap))
		{
			return;
		}
		for (const TPair<FName, FAsceActionDef>& Pair : RowMap)
		{
			GActionDefRows.Add(Pair.Key, Pair.Value);
			if (Pair.Value.ActionId > 0)
			{
				GActionDefsById.Add(Pair.Value.ActionId, Pair.Value);
			}
		}
	}
}

bool FAsceActionDefLibrary::GetActionDef(const int32 ActionId, FAsceActionDef& OutDef)
{
	// 0 = 内置攻击动作（没有表行）。改成"攻击 = 一行 Auto 动作"之后，攻击行有自己的 ActionId（>0）
	if (ActionId <= 0)
	{
		return false;
	}
	EnsureBuilt();
	if (const FAsceActionDef* Found = GActionDefsById.Find(ActionId))
	{
		OutDef = *Found;
		return true;
	}
	return false;
}

bool FAsceActionDefLibrary::GetActionDefByRowName(const FName RowName, FAsceActionDef& OutDef)
{
	if (RowName.IsNone())
	{
		return false;
	}
	EnsureBuilt();
	if (const FAsceActionDef* Found = GActionDefRows.Find(RowName))
	{
		OutDef = *Found;
		return true;
	}
	return false;
}

void FAsceActionDefLibrary::InvalidateCache()
{
	GActionDefRows.Reset();
	GActionDefsById.Reset();
	GBuilt = false;
}
UStateTree* FAsceActionDefLibrary::ResolveSkillTree(const FAsceActionDef& ActionDef)
{
	if (ActionDef.SkillTree)
	{
		return ActionDef.SkillTree;
	}
	if (ActionDef.ReleaseMode != EAsceReleaseMode::Auto)
	{
		return nullptr;   // 技能必须自己配树；只有"攻击"才有默认树回落
	}

	const UAsceGameplaySettings* Settings = GetDefault<UAsceGameplaySettings>();
	if (!Settings)
	{
		return nullptr;
	}
	return ActionDef.RangeKind == EAsceRangeKind::MeleeSurface
		? Settings->DefaultMeleeAttackTree.LoadSynchronous()
		: Settings->DefaultRangedAttackTree.LoadSynchronous();
}
