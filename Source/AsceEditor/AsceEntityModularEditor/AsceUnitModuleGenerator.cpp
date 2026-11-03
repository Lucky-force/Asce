// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/AsceUnitModuleGenerator.h"

#include "MassAssortedFragmentsTrait.h"
#include "MassEntityTraitBase.h"
#include "MassEntityConfigAsset.h"
#include "MassActorSubsystem.h"
#include "MassLODFragments.h"
#include "MassLODTrait.h"
#include "MassMovableVisualizationTrait.h"
#include "MassRepresentationTypes.h"
#include "Misc/Crc.h"
#include "UObject/UnrealType.h"

#include "AsceMass/Data/AsceEntityDefTypes.h"
#include "AsceMass/Data/AsceUnitDef.h"
#include "AsceMass/Building/AsceBuildingDef.h"
#include "AsceMass/Traits/AsceRTSEntityTrait.h"
#include "AsceMass/Traits/AsceRTSUnitTrait.h"
#include "GameModes/AsceGameplaySettings.h"

#include "AsceEntityModularEditor/AsceUnitModule.h"
#include "AsceEntityModularEditor/AsceUnitModuleRegistry.h"

namespace
{
	/** 模块列表稳定哈希：按反射属性导出文本（不能用原始内存：含 vptr/TArray 指针，跨会话不稳定） */
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
			for (TFieldIterator<FProperty> It(Type); It; ++It)
			{
				FString ValueText;
				It->ExportTextItem_Direct(ValueText, It->ContainerPtrToValuePtr<void>(Module.GetMemory())
					, nullptr, nullptr, PPF_None);
				Hash = HashCombine(Hash, GetTypeHash(It->GetFName()));
				Hash = FCrc::StrCrc32(*ValueText, Hash);
			}
		}
		return Hash;
	}

	void DedupeByType(TArray<FInstancedStruct>& Fragments)
	{
		TSet<const UScriptStruct*> Seen;
		for (auto It = Fragments.CreateIterator(); It; ++It)
		{
			const UScriptStruct* Type = It->GetScriptStruct();
			if (!Type || Seen.Contains(Type))
			{
				It.RemoveCurrent();
				continue;
			}
			Seen.Add(Type);
		}
	}

	void DedupeTypes(TArray<const UScriptStruct*>& Types)
	{
		TSet<const UScriptStruct*> Seen;
		for (auto It = Types.CreateIterator(); It; ++It)
		{
			const UScriptStruct* Type = *It;
			if (!Type || Seen.Contains(Type))
			{
				It.RemoveCurrent();
				continue;
			}
			Seen.Add(Type);
		}
	}

	/** 反射填充 `TArray<FInstancedStruct>` 属性（UMassAssortedFragmentsTrait 的 Fragments/Tags 是 protected） */
	/** 单位用 VAT/ISM 表现：所有 LOD 都固定走 StaticMeshInstance，避免高/中 LOD 因没有 Actor 模板反复切换导致闪烁 */
	void ConfigureMovableVisualization(UMassMovableVisualizationTrait& Trait)
	{
		const UAsceGameplaySettings* Settings = GetDefault<UAsceGameplaySettings>();
		if (!Settings || !Settings->bOverrideUnitVisualizationLOD)
		{
			return;
		}

		Trait.Params = Settings->UnitRepresentationParams;
		Trait.LODParams = Settings->UnitVisualizationLODParams;
		Trait.HighResTemplateActor = Settings->HighResTemplateActor;
		Trait.LowResTemplateActor = Settings->LowResTemplateActor;
		Trait.bAllowServerSideVisualization = Settings->bAllowServerSideVisualization;
	}

	/** 把旧模板上的 UAsceRTSUnitTrait 移除；模块化后它的 fragment 由 AssortedFragments + def 接管 */
	void RemoveLegacyUnitTrait(UMassEntityConfigAsset& ConfigAsset)
	{
		FArrayProperty* TraitsProperty = FindFProperty<FArrayProperty>(FMassEntityConfig::StaticStruct(), TEXT("Traits"));
		if (!TraitsProperty)
		{
			return;
		}

		FMassEntityConfig& Config = ConfigAsset.GetMutableConfig();
		FScriptArrayHelper Helper(TraitsProperty, TraitsProperty->ContainerPtrToValuePtr<void>(&Config));
		const FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(TraitsProperty->Inner);
		if (!Inner)
		{
			return;
		}

		for (int32 Index = Helper.Num() - 1; Index >= 0; --Index)
		{
			UObject* Trait = Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index));
			if (Trait && Trait->IsA(UAsceRTSUnitTrait::StaticClass()))
			{
				UE_LOG(LogTemp, Warning, TEXT("[AsceModules] 移除旧模板 trait %s：%s")
					, *ConfigAsset.GetPathName(), *Trait->GetName());
				Helper.RemoveValues(Index, 1);
			}
		}
		ConfigAsset.MarkPackageDirty();
	}

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

	// 默认给一个 MassActorFragment（可视化/表示层需要；模块若也贡献，后面去重）
	Result.Fragments.Add(FInstancedStruct::Make<FMassActorFragment>());

	// 记录模块实例，供 WriteToTemplate（例如 Visualization 写 MovableVisualizationTrait）
	for (const FInstancedStruct& Module : Modules)
	{
		if (const FAsceUnitModule* Base = Module.GetPtr<FAsceUnitModule>())
		{
			Result.Modules.Add(Base);
		}
	}

	DedupeByType(Result.Fragments);
	DedupeTypes(Result.Tags);
	return Result;
}

bool AsceUnitModuleGenerator::EnsureDefaultTemplateTraits(UMassEntityConfigAsset* ConfigAsset, const FName RowName, TArray<FText>& OutErrors)
{
	if (!ConfigAsset)
	{
		OutErrors.Add(INVTEXT("模板资产为空"));
		return false;
	}

	ConfigAsset->Modify();

	if (UAsceRTSEntityTrait* EntityTrait = Cast<UAsceRTSEntityTrait>(ConfigAsset->AddTrait(UAsceRTSEntityTrait::StaticClass())))
	{
		if (EntityTrait->UniqueType.RowName.IsNone())
		{
			EntityTrait->UniqueType.RowName = RowName;
		}
	}
	ConfigAsset->AddTrait(UMassDistanceLODCollectorTrait::StaticClass());
	if (UMassMovableVisualizationTrait* Visualization = Cast<UMassMovableVisualizationTrait>(
		ConfigAsset->AddTrait(UMassMovableVisualizationTrait::StaticClass())))
	{
		ConfigureMovableVisualization(*Visualization);
	}

	UMassAssortedFragmentsTrait* Assorted = Cast<UMassAssortedFragmentsTrait>(
		ConfigAsset->AddTrait(UMassAssortedFragmentsTrait::StaticClass()));
	if (!Assorted)
	{
		OutErrors.Add(INVTEXT("创建 UMassAssortedFragmentsTrait 失败"));
		return false;
	}

	FArrayProperty* FragmentsProperty = FindFProperty<FArrayProperty>(Assorted->GetClass(), TEXT("Fragments"));
	if (!FragmentsProperty)
	{
		OutErrors.Add(INVTEXT("找不到 AssortedFragmentsTrait.Fragments"));
		return false;
	}

	FScriptArrayHelper Helper(FragmentsProperty, FragmentsProperty->ContainerPtrToValuePtr<void>(Assorted));
	for (int32 Index = 0; Index < Helper.Num(); ++Index)
	{
		const FInstancedStruct* Fragment = reinterpret_cast<const FInstancedStruct*>(Helper.GetRawPtr(Index));
		if (Fragment && Fragment->GetScriptStruct() == FMassActorFragment::StaticStruct())
		{
			ConfigAsset->MarkPackageDirty();
			return true;
		}
	}

	const int32 Index = Helper.AddValue();
	*reinterpret_cast<FInstancedStruct*>(Helper.GetRawPtr(Index)) = FInstancedStruct::Make<FMassActorFragment>();
	ConfigAsset->MarkPackageDirty();
	return true;
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

	// 模块化模板只应保留基础 trait + Assorted Fragments；旧的 UAsceRTSUnitTrait 会先写入
	// Action/Visual/Anim/Skill fragment，导致模块片段值被“已有 fragment”跳过。
	if (ConfigAsset->FindTrait(UAsceRTSUnitTrait::StaticClass(), /*bExactMatch*/ true))
	{
		if (Result.Modules.Num() > 0)
		{
			RemoveLegacyUnitTrait(*ConfigAsset);
		}
		else
		{
			OutErrors.Add(INVTEXT("模板仍有 UAsceRTSUnitTrait；请移除该 trait，或先给 ModuleConfig 添加模块后 Apply"));
			return false;
		}
	}

	ConfigAsset->Modify();

	// 默认表示层/LOD trait：模板创建或第一次 Apply 时补齐
	ConfigAsset->AddTrait(UMassDistanceLODCollectorTrait::StaticClass());
	if (UMassMovableVisualizationTrait* Visualization = Cast<UMassMovableVisualizationTrait>(
		ConfigAsset->AddTrait(UMassMovableVisualizationTrait::StaticClass())))
	{
		ConfigureMovableVisualization(*Visualization);
	}

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

	// 各模块写自己的模板 trait（例如 Visualization 写 MassMovableVisualizationTrait 的 Mesh/LocalTransform）
	for (const FAsceUnitModule* Module : Result.Modules)
	{
		if (Module)
		{
			Module->WriteToTemplate(*ConfigAsset);
		}
	}

	ConfigAsset->MarkPackageDirty();
	return true;
}

bool AsceUnitModuleGenerator::WriteToDefRow(const TArray<FInstancedStruct>& Modules, FAsceEntityDefRow& Row, TArray<FText>& OutErrors)
{
	TArray<FText> ModuleErrors;
	AsceUnitModuleRegistry::ValidateModules(Modules, ModuleErrors);
	if (ModuleErrors.Num() > 0)
	{
		OutErrors.Append(ModuleErrors);
		return false;
	}

	if (FAsceRTSUnitDef* UnitDef = Row.Def.GetMutablePtr<FAsceRTSUnitDef>())
	{
		AsceUnitModuleRegistry::ApplyDefFields(Modules, *UnitDef);
		return true;
	}

	// 建筑 / Ether 建筑没有单位属性字段；模块类型已经决定，保留 Def 上的建筑字段即可。
	const UScriptStruct* DefType = Row.Def.GetScriptStruct();
	if (DefType && DefType->IsChildOf(FAsceBuildingDefBase::StaticStruct()))
	{
		return true;
	}

	OutErrors.Add(INVTEXT("def 的 Def 必须是 FAsceRTSUnitDef 或 FAsceBuildingDefBase 的派生结构"));
	return false;
}
