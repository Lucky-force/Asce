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
// 阵营：TeamKey = (TeamIndex << 16) | TeamId（与 AsceVisionDefines.h 同一口径）
// ──────────────────────────────────────────
// - **TeamId**（低 16 位）= **跨端一致**的身份（玩家看 AAscePlayerState::MyTeamID；NoTeam → 0）。
//   敌我判断 / 指挥授权校验一律用它。
// - **TeamIndex**（高 16 位）= **本端**的数组下标（空间层列、迷雾行），先来先得，各端可以不一样
//   ⇒ 只用来索引数组，**不用于比较、不跨端传**；分配点在 `AAsceGameState`（本端阵营表）。
// - 0 = 无阵营/中立（资源这类中立物也是 0，**可被攻击**）；玩家队由 GameState 先从 PlayerArray 登记。
inline constexpr int32 AsceMaxTeams = 8;
inline constexpr int32 AsceNeutralTeamId = 0;
inline constexpr int32 AsceTeamKeyShift = 16;
inline constexpr int32 AsceTeamIdMask = 0xFFFF;

/** 打包 TeamKey → TeamId（低 16 位；跨端一致，用于比较/授权） */
FORCEINLINE int32 AsceTeamId(int32 TeamKey) { return TeamKey & AsceTeamIdMask; }

/** 打包 TeamKey → TeamIndex（高 16 位；**本端**数组下标。别直接拿它索引数组，先用 AAsceGameState 重映射） */
FORCEINLINE int32 AsceTeamIndex(int32 TeamKey) { return TeamKey >> AsceTeamKeyShift; }

/** 打包（TeamIndex 由本端阵营表给出：`AAsceGameState::GetTeamKey`） */
FORCEINLINE int32 AsceMakeTeamKey(int32 TeamIndex, int32 TeamId)
{
	return (TeamIndex << AsceTeamKeyShift) | (TeamId & AsceTeamIdMask);
}

/** 归一化**裸** TeamId（玩家/定义填的原始队号）：<=0 / NoTeam / 越界一律归 0 中立 */
FORCEINLINE int32 AsceNormalizeTeamId(int32 RawTeamId)
{
	return (RawTeamId > 0 && RawTeamId <= AsceTeamIdMask) ? RawTeamId : AsceNeutralTeamId;
}

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
