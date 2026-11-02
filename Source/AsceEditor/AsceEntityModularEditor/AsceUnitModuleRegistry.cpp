// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/AsceUnitModuleRegistry.h"

#include "AsceEntityModularEditor/Modules/AsceUnitModuleActionState.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleAnimation.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleBuilding.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleCombat.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleEther.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleHealth.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleInteraction.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleMovement.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleSkills.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleVision.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleVisualization.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleVolume.h"

#include "AsceMass/Data/AsceUnitDef.h"

namespace
{
	bool HasFragmentType(const TArray<FInstancedStruct>& Fragments, const UScriptStruct* Type)
	{
		for (const FInstancedStruct& Fragment : Fragments)
		{
			if (Fragment.GetScriptStruct() == Type)
			{
				return true;
			}
		}
		return false;
	}

	/** 所选模块 → 实例（按 ModuleId 去重；重复/未知写入 OutErrors） */
	void CollectSelected(const TArray<FInstancedStruct>& Modules, TMap<FName, const FAsceUnitModule*>& OutSelected, TArray<FText>& OutErrors)
	{
		for (const FInstancedStruct& Module : Modules)
		{
			const FAsceUnitModule* Base = Module.GetPtr<FAsceUnitModule>();
			if (!Base)
			{
				OutErrors.Add(INVTEXT("模块实例为空或类型无效"));
				continue;
			}
			const FName ModuleId = Base->GetModuleId();
			if (OutSelected.Contains(ModuleId))
			{
				OutErrors.Add(FText::FromString(FString::Printf(TEXT("模块 [%s] 重复"), *ModuleId.ToString())));
				continue;
			}
			OutSelected.Add(ModuleId, Base);
		}
	}

	const FAsceUnitModule* FindSelected(const TMap<FName, const FAsceUnitModule*>& Selected, const UScriptStruct* Type)
	{
		if (!Type)
		{
			return nullptr;
		}
		// 用一个默认实例拿 ModuleId（编辑器侧，量很小）
		FInstancedStruct Probe;
		Probe.InitializeAs(Type, nullptr);
		const FAsceUnitModule* Base = Probe.GetPtr<FAsceUnitModule>();
		if (!Base)
		{
			return nullptr;
		}
		const FAsceUnitModule* const* Found = Selected.Find(Base->GetModuleId());
		return Found ? *Found : nullptr;
	}
}

const TArray<const UScriptStruct*>& AsceUnitModuleRegistry::GetModuleTypes()
{
	// 顺序即"合成时的落地顺序"：**拥有参数的模块排在前面**（Volume 先于 Movement/Vision/Interaction）
	static const TArray<const UScriptStruct*> Types =
	{
		FAsceUnitModuleHealth::StaticStruct(),
		FAsceUnitModuleVolume::StaticStruct(),
		FAsceUnitModuleMovement::StaticStruct(),
		FAsceUnitModuleVision::StaticStruct(),
		FAsceUnitModuleActionState::StaticStruct(),
		FAsceUnitModuleAnimation::StaticStruct(),
		FAsceUnitModuleVisualization::StaticStruct(),
		FAsceUnitModuleCombat::StaticStruct(),
		FAsceUnitModuleSkills::StaticStruct(),
		FAsceUnitModuleInteraction::StaticStruct(),
		FAsceUnitModuleBuilding::StaticStruct(),
		FAsceUnitModuleEther::StaticStruct(),
	};
	return Types;
}

FInstancedStruct AsceUnitModuleRegistry::MakeDefaultModule(const FName ModuleId)
{
	for (const UScriptStruct* Type : GetModuleTypes())
	{
		FInstancedStruct Instance;
		Instance.InitializeAs(Type, nullptr);
		const FAsceUnitModule* Base = Instance.GetPtr<FAsceUnitModule>();
		if (Base && Base->GetModuleId() == ModuleId)
		{
			return Instance;
		}
	}
	return FInstancedStruct();
}

void AsceUnitModuleRegistry::BuildComposition(const TArray<FInstancedStruct>& Modules, TArray<FInstancedStruct>& OutFragments
	, TArray<const UScriptStruct*>& OutTags, TArray<FText>& OutErrors)
{
	OutFragments.Reset();
	OutTags.Reset();

	TMap<FName, const FAsceUnitModule*> Selected;
	CollectSelected(Modules, Selected, OutErrors);

	FAsceUnitModuleBuildContext Context;
	Context.Modules = &Modules;

	// 按注册顺序合成：同类型 fragment/tag 只保留第一份（拥有者先落地）
	for (const UScriptStruct* Type : GetModuleTypes())
	{
		const FAsceUnitModule* Module = FindSelected(Selected, Type);
		if (!Module)
		{
			continue;
		}

		TArray<FInstancedStruct> Contributed;
		TArray<const UScriptStruct*> ContributedTags;
		Module->Contribute(Context, Contributed, ContributedTags);

		for (FInstancedStruct& Fragment : Contributed)
		{
			if (!HasFragmentType(OutFragments, Fragment.GetScriptStruct()))
			{
				OutFragments.Add(MoveTemp(Fragment));
			}
		}
		for (const UScriptStruct* Tag : ContributedTags)
		{
			OutTags.AddUnique(Tag);
		}
	}
}

void AsceUnitModuleRegistry::ApplyDefFields(const TArray<FInstancedStruct>& Modules, FAsceRTSUnitDef& Row)
{
	// 模块列表是单位定义的唯一真相：先清空所有模块字段，再按当前选择重写，
	// 否则取消某个模块后旧字段（AttackAction/AnimSet/SkillSlots…）会残留在行里。
	const TSoftObjectPtr<UStateTree> SavedStateTree = Row.StateTree;
	Row = FAsceRTSUnitDef();
	Row.StateTree = SavedStateTree;

	TMap<FName, const FAsceUnitModule*> Collected;
	TArray<FText> Ignored;
	CollectSelected(Modules, Collected, Ignored);

	for (const UScriptStruct* Type : GetModuleTypes())
	{
		if (const FAsceUnitModule* Module = FindSelected(Collected, Type))
		{
			Module->WriteDefRow(Row);
		}
	}
}

void AsceUnitModuleRegistry::ValidateModules(const TArray<FInstancedStruct>& Modules, TArray<FText>& OutErrors)
{
	TSet<FName> SeenModules;
	for (const FInstancedStruct& Module : Modules)
	{
		const FAsceUnitModule* Base = Module.GetPtr<FAsceUnitModule>();
		if (!Base)
		{
			OutErrors.Add(INVTEXT("模块实例为空或类型无效"));
			continue;
		}

		const FName ModuleId = Base->GetModuleId();
		if (SeenModules.Contains(ModuleId))
		{
			OutErrors.Add(FText::FromString(FString::Printf(TEXT("模块 [%s] 重复"), *ModuleId.ToString())));
			continue;
		}
		SeenModules.Add(ModuleId);

		bool bKnown = false;
		for (const UScriptStruct* Type : GetModuleTypes())
		{
			FInstancedStruct Probe;
			Probe.InitializeAs(Type, nullptr);
			const FAsceUnitModule* Candidate = Probe.GetPtr<FAsceUnitModule>();
			if (Candidate && Candidate->GetModuleId() == ModuleId)
			{
				bKnown = true;
				break;
			}
		}
		if (!bKnown)
		{
			OutErrors.Add(FText::FromString(FString::Printf(TEXT("未知模块 [%s]（注册表里没有）"), *ModuleId.ToString())));
			continue;
		}

		Base->Validate(OutErrors);
	}
}
