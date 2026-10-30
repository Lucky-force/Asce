// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/AsceUnitModuleGenerator.h"

#include "MassAssortedFragmentsTrait.h"
#include "MassEntityTraitBase.h"
#include "MassEntityConfigAsset.h"
#include "Misc/Crc.h"
#include "UObject/UnrealType.h"

#include "AsceMass/Data/AsceEntityDefTypes.h"
#include "AsceMass/Data/AsceUnitDef.h"

#include "AsceEntityModularEditor/AsceUnitModuleRegistry.h"

namespace
{
	/** 模块列表的稳定哈希（类型 + 结构体内存；同一构建内稳定，够做漂移检测） */
	uint32 HashModules(const TArray<FInstancedStruct>& Modules)
	{
		uint32 Hash = 0;
		for (const FInstancedStruct& Module : Modules)
		{
			const UScriptStruct* Type = Module.GetScriptStruct();
			if (!Type || !Module.GetMemory())
			{
				continue;
			}
			Hash = HashCombine(Hash, GetTypeHash(Type->GetFName()));
			Hash = FCrc::MemCrc32(Module.GetMemory(), Type->GetStructureSize(), Hash);
		}
		return Hash;
	}

	/** 反射填充 `TArray<FInstancedStruct>` 属性（UMassAssortedFragmentsTrait 的 Fragments/Tags 是 protected） */
	bool FillInstanceArray(UObject* Owner, const FName PropertyName, const TArray<FInstancedStruct>& Values, TArray<FText>& OutErrors)
	{
		FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(Owner->GetClass(), PropertyName);
		if (!ArrayProperty)
		{
			OutErrors.Add(FText::FromString(FString::Printf(TEXT("找不到属性 %s"), *PropertyName.ToString())));
			return false;
		}

		FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Owner));
		Helper.EmptyValues();
		for (const FInstancedStruct& Value : Values)
		{
			const int32 Index = Helper.AddValue();
			FInstancedStruct* Destination = reinterpret_cast<FInstancedStruct*>(Helper.GetRawPtr(Index));
			*Destination = Value;
		}
		return true;
	}
}

FAsceUnitModuleBuildResult AsceUnitModuleGenerator::Build(const TArray<FInstancedStruct>& Modules)
{
	FAsceUnitModuleBuildResult Result;
	Result.SourceHash = HashModules(Modules);

	AsceUnitModuleRegistry::ValidateModules(Modules, Result.Errors);
	AsceUnitModuleRegistry::BuildComposition(Modules, Result.Fragments, Result.Tags, Result.Errors);
	return Result;
}

bool AsceUnitModuleGenerator::WriteToTemplateAsset(UMassEntityConfigAsset* ConfigAsset, const FAsceUnitModuleBuildResult& Result, TArray<FText>& OutErrors)
{
	if (!ConfigAsset)
	{
		OutErrors.Add(INVTEXT("模板资产为空"));
		return false;
	}
	if (!Result.IsValid())
	{
		OutErrors.Append(Result.Errors);
		return false;
	}

	ConfigAsset->Modify();

	// 引擎自带 trait：`Fragments` / `Tags` 都是 `TArray<FInstancedStruct>`（protected，用反射写）
	UMassEntityTraitBase* Trait = ConfigAsset->AddTrait(UMassAssortedFragmentsTrait::StaticClass());
	if (!Trait)
	{
		OutErrors.Add(INVTEXT("创建 UMassAssortedFragmentsTrait 失败"));
		return false;
	}

	TArray<FInstancedStruct> TagInstances;
	TagInstances.Reserve(Result.Tags.Num());
	for (const UScriptStruct* TagType : Result.Tags)
	{
		if (TagType)
		{
			FInstancedStruct Tag;
			Tag.InitializeAs(TagType, nullptr);
			TagInstances.Add(MoveTemp(Tag));
		}
	}

	const bool bFragmentsOk = FillInstanceArray(Trait, TEXT("Fragments"), Result.Fragments, OutErrors);
	const bool bTagsOk = FillInstanceArray(Trait, TEXT("Tags"), TagInstances, OutErrors);
	if (!bFragmentsOk || !bTagsOk)
	{
		return false;
	}

	ConfigAsset->MarkPackageDirty();
	return true;
}

bool AsceUnitModuleGenerator::WriteToDefRow(const TArray<FInstancedStruct>& Modules, FAsceEntityDefRow& Row, TArray<FText>& OutErrors)
{
	FAsceRTSUnitDef* UnitDef = Row.Def.GetMutablePtr<FAsceRTSUnitDef>();
	if (!UnitDef)
	{
		OutErrors.Add(INVTEXT("def 行的 Def 不是 FAsceRTSUnitDef"));
		return false;
	}

	TArray<FText> ModuleErrors;
	AsceUnitModuleRegistry::ValidateModules(Modules, ModuleErrors);
	if (ModuleErrors.Num() > 0)
	{
		OutErrors.Append(ModuleErrors);
		return false;
	}

	AsceUnitModuleRegistry::ApplyDefFields(Modules, *UnitDef);
	return true;
}
