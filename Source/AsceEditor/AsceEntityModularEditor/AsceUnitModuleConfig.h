// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StructUtils/InstancedStruct.h"

#include "AsceUnitModuleConfig.generated.h"

struct FAsceEntityDefRow;

/**
 * **单位类型（= 实体定义表一行）的模块配置**（编辑器专属资产）。
 *
 * 只有编辑器会加载它：运行时只看 def 行 + trait DA，看不到这个资产，也看不到"模块"这个概念。
 *
 * - `Modules`：源数据（勾了哪些模块 + 参数）；
 * - `SourceHash`：上次 `ApplyToDef` 时模块列表的哈希 ⇒ 用来判断"模块改过、还没重新应用"。
 *
 * 见 `AsceMass/Data/单位模块化设计.md` §5。
 */
UCLASS()
class UAsceUnitModuleConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 模块列表（源数据；参数写在各模块结构里） */
	UPROPERTY(EditAnywhere, Category = "Modules",
		meta = (BaseStruct = "/Script/AsceEditor.AsceUnitModule", ExcludeBaseStruct))
	TArray<FInstancedStruct> Modules;

	/** 上次应用时的模块哈希（`IsDirty()` 用） */
	UPROPERTY(VisibleAnywhere, Category = "Modules")
	uint32 SourceHash = 0;

	/** 从 def 行读出模块配置资产（没配 / 加载失败返回 nullptr） */
	static UAsceUnitModuleConfig* LoadForDef(const FAsceEntityDefRow& Row);

	/**
	 * 把当前模块应用到 def 行：
	 *   ① `Build`（并集 + 校验）② 写 def 行的模板资产（`UMassAssortedFragmentsTrait`）
	 *   ③ 写 def 的非 fragment 字段 ④ 记录 `SourceHash`
	 * **不负责保存资产**（调用者按需 `SavePackage`）。
	 */
	bool ApplyToDef(FAsceEntityDefRow& Row, TArray<FText>& OutErrors);

	/** 模块列表在应用后是否被改过（需要重新 `ApplyToDef`） */
	bool IsDirty() const;
};
