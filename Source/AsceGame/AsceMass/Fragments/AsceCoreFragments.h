// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "MassEntityTypes.h"
#include "AsceCoreFragments.generated.h"

/**
 * 通用核心Fragment：任何"会动/会死/属于某方"的Entity都可能挂这些。
 * 注意：不放"是否被选中"这类客户端本地UI状态——那是每个玩家客户端自己的概念（谁选中了谁），
 * 不是Entity自身的属性，不应该作为同步数据广播给所有人。选中状态由客户端的Commander组件本地持有。
 */

// ──────────────────────────────────────────
// 血量
// ──────────────────────────────────────────

USTRUCT(BlueprintType)
struct FAsceHealthFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	float CurrentHealth = 100.f;

	UPROPERTY(BlueprintReadWrite)
	float MaxHealth = 100.f;
};

// ──────────────────────────────────────────
// 阵营
// ──────────────────────────────────────────

USTRUCT(BlueprintType)
struct FAsceTeamFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	int32 TeamKey = 0;
};

// ──────────────────────────────────────────
// 高度（不影响寻路，只影响视野/表现，寻路系统本身无视高度）
// ──────────────────────────────────────────

USTRUCT(BlueprintType)
struct FAsceHeightFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	float Height = 100.f;
};

// ──────────────────────────────────────────
// Tags
// ──────────────────────────────────────────

USTRUCT()
struct FAsceAliveTag : public FMassTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FAsceDeadTag : public FMassTag
{
	GENERATED_BODY()
};

/** 建筑（有 `FAsceGridFootprintFragment` 的静态实体；SpatialGrid 的"建筑层"与 Ether 等消费者按它筛选）。 */
USTRUCT()
struct FAsceBuildingTag : public FMassTag
{
	GENERATED_BODY()
};

/** 可移动单位（会被 SpatialGrid 的"单位层"收录；子弹/投射物等以后各自加 tag）。 */
USTRUCT()
struct FAsceMovableUnitTag : public FMassTag
{
	GENERATED_BODY()
};

/** 实体类型定义 id（= `FAsceEntityDefRow` 的数字行名，0..65535；`UAsceEntityDefLibrary` 查表）。
 *  只存 uint16：跨 session/两端一致，改名不改数（编号只增不复用）。 */
USTRUCT()
struct FAsceUnitDefFragment : public FMassFragment
{
	GENERATED_BODY()

	uint16 UniqueTypeId = 0;
};
