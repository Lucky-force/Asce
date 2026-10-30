// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"

struct FAsceRTSUnitDef;

/**
 * 模块注册表（**AsceEditor**，纯代码，无资产）。
 *
 * 模块的"知识"在各自的 USTRUCT 里（虚函数 `Contribute / WriteDefRow / Validate`），
 * 这里只负责：**列出类型**、**按顺序合成并集**、**写 def 非 fragment 字段**、**校验**。
 *
 * 用法（P3 生成器）：
 *   BuildComposition(所选模块) → 写 trait DA（`UMassAssortedFragmentsTrait`）
 *   → ApplyDefFields(...) → 写 def 行
 *
 * 见 `AsceMass/Data/单位模块化设计.md` §5。
 */
namespace AsceUnitModuleRegistry
{
	/** 所有模块类型（"加模块"菜单 / 合成顺序用；顺序稳定：先拥有参数者先落地） */
	const TArray<const UScriptStruct*>& GetModuleTypes();

	/** 新建某模块的默认实例（`FInstancedStruct` 内含具体 struct） */
	FInstancedStruct MakeDefaultModule(FName ModuleId);

	/**
	 * 模块并集 → fragment/tag（带初值）。
	 * - 同类型 fragment/tag 只保留一份，**先出现的（按 GetModuleTypes 顺序）决定值**；
	 * - 重复模块 / 未知模块写进 `OutErrors`。
	 */
	void BuildComposition(const TArray<FInstancedStruct>& Modules, TArray<FInstancedStruct>& OutFragments
		, TArray<const UScriptStruct*>& OutTags, TArray<FText>& OutErrors);

	/** 模块 → def 的非 fragment 字段（`AnimSet` / `AttackAction` / `SkillSlots`） */
	void ApplyDefFields(const TArray<FInstancedStruct>& Modules, FAsceRTSUnitDef& Row);

	/** 全量校验：重复 / 未知 / 各模块自己的 `Validate` */
	void ValidateModules(const TArray<FInstancedStruct>& Modules, TArray<FText>& OutErrors);
}
