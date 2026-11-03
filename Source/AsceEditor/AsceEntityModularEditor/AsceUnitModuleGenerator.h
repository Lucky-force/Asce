// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"

class UMassEntityConfigAsset;
struct FAsceEntityDefRow;
struct FAsceUnitModule;

/** 一次"模块 → 数据"的构建结果 */
struct FAsceUnitModuleBuildResult
{
	/** fragment 并集（**带初值**），供写 `UMassAssortedFragmentsTrait::Fragments` */
	TArray<FInstancedStruct> Fragments;

	/** tag 并集（类型；写入 DA 时再构造成 `FInstancedStruct`） */
	TArray<const UScriptStruct*> Tags;

	/** 校验/合成过程中的错误（非空 = 不可用） */
	TArray<FText> Errors;

	/** 模块列表的稳定哈希（用于"派生数据被手改"的漂移检测；P4 存进配置资产） */
	uint32 SourceHash = 0;

	/** 当前模块实例（WriteToTemplate 用；生命周期跟随 Build 的输入） */
	TArray<const FAsceUnitModule*> Modules;

	bool IsValid() const { return Errors.Num() == 0; }
};

/**
 * **模块 → 数据** 的生成器（编辑器侧）。
 *
 * - `Build`：并集 + 校验 + 哈希（纯内存）；
 * - `WriteToTemplateAsset`：写进 `UMassEntityConfigAsset`（复用/新增引擎自带的
 * - `WriteToDefRow`：把非 fragment 字段（`AnimSet` / `AttackAction` / `SkillSlots`）写进 def 行。
 *
 * 见 `AsceMass/Data/单位模块化设计.md` §5。
 */
namespace AsceUnitModuleGenerator
{
	/** 纯内存：合成并集 + 校验 + 哈希 */
	FAsceUnitModuleBuildResult Build(const TArray<FInstancedStruct>& Modules);

	/** 把构建结果写进 trait DA（会 `Modify()` + `MarkPackageDirty()`） */
	bool WriteToTemplateAsset(UMassEntityConfigAsset* ConfigAsset, const FAsceUnitModuleBuildResult& Result, TArray<FText>& OutErrors);

	/** 补齐模板默认 trait：AsceRTSEntityTrait / DistanceLODCollector / MovableVisualization + Assorted MassActor（不覆盖已有 Fragments） */
	bool EnsureDefaultTemplateTraits(UMassEntityConfigAsset* ConfigAsset, FName RowName, TArray<FText>& OutErrors);

	/** 把模块写回实体定义：FAsceRTSUnitDef 写单位字段；建筑 Def 保留建筑字段。 */
	bool WriteToDefRow(const TArray<FInstancedStruct>& Modules, FAsceEntityDefRow& Row, TArray<FText>& OutErrors);
}
