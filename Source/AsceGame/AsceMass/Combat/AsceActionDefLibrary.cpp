// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceActionDefLibrary.h"

#include "DataTableSystem/AsceDataTableSubsystem.h"

bool FAsceActionDefLibrary::GetActionDef(int32 ActionId, FAsceActionDef& OutDef)
{
	// 0 = 内置攻击动作：参数在 FAsceCombatFragment 上，不查表
	if (ActionId <= 0)
	{
		return false;
	}

	TMap<FName, FAsceActionDef> RowMap;
	if (!UAsceDataTableSubsystem::GetDefinitionMap<FAsceActionDef>(RowMap))
	{
		return false;   // 没登记"行结构 = FAsceActionDef"的 MDT（或表为空）
	}

	for (const TPair<FName, FAsceActionDef>& Pair : RowMap)
	{
		if (Pair.Value.ActionId == ActionId)
		{
			OutDef = Pair.Value;
			return true;
		}
	}
	return false;
}
