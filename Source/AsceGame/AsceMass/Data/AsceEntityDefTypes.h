// AsceEntityDefTypes.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "StructUtils/InstancedStruct.h"
#include "AsceEntityDefTypes.generated.h"

class UMassEntityConfigAsset;
class UStateTree;

/**
 * 实体类型定义（所有"可选中的实体"通用）：
 * - **trait 决定结构**（fragment/tag 组合 = archetype）；
 * - **本表决定类型数据**：UI 表现列 + `Def`（各类型专属配置的 `FInstancedStruct`，基类 `FAsceEntityDefBase`）。
 *
 * 表（MDT）由 `UAsceDataTableSettings::AsceEntityDefRow` 登记；**行名 = 数字 ID 字符串**（0..65535，
 * 只增不复用），实体上只存 `FAsceUnitDefFragment::UniqueTypeId`（uint16）。
 * 查询见 `UAsceEntityDefLibrary`。
 */
USTRUCT(BlueprintType)
struct FAsceEntityDefBase
{
	GENERATED_BODY()

	// 这里只放"所有类型都成立"的字段。
	// 建筑占地/遮挡/core → `FAsceBuildingDefBase`（`UAsceRTSBuildingTrait` 配默认值）；
	// 单位半径/高度/速度等 → `FAsceRTSUnitDef`。

	/** **该类型的行为树**（Mass × StateTree；`Combat/技能编辑器设计.md`）：每类型一棵，由
	 *  `UAsceRTSEntityTrait::BuildTemplate` **两端都**挂进实体模板（客户端也跑表现 task）。
	 *  必须用 **Mass StateTree Schema**（`UMassStateTreeSchema`）建；空 = 该类型没有状态树（行为不变）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior")
	TSoftObjectPtr<UStateTree> StateTree;
};

/** 实体类型表的一行（MDT；行名 = 数字 ID 字符串）。 */
USTRUCT(BlueprintType)
struct FAsceEntityDefRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 类型专属配置（如 `FAsceEtherBuildingDef`；读取时先判 `struct` 类型）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Def",
		meta = (BaseStruct = "/Script/AsceGame.AsceEntityDefBase", ExcludeBaseStruct = true))
	FInstancedStruct Def;

	/** 生成模板（`UMassEntityConfigAsset`；`UAsceEntityFactory::SpawnEntitiesByDef` 按它建实体）。
	 *  非工厂路径（建筑子系统自己创建）可空。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Def")
	TSoftObjectPtr<UMassEntityConfigAsset> EntityTemplate;

	// ── UI 表现（所有可选中实体通用；检视器用） ──

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	TSoftObjectPtr<UTexture2D> HeadIcon;
};
