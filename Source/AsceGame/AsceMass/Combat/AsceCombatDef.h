// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

#include "AsceCombatDef.generated.h"

/**
 * 动作 / 技能定义（数据表行；服务器与客户端共用同一份）。
 *
 * 见 `技能系统设计.md`：
 *  - **攻击 = 内置动作定义**（自动触发、无蓝耗），技能 = 玩家触发的动作定义；
 *    两者共用同一套"占时 → 真正释放 → 结算 → 打断"管线。
 *  - "**真正释放**" = 处理 cost + CD + effect 的那一帧：`Cast` = 前摇结束，`Channel`/`Instant` = 开始。
 */

/** 动作状态（唯一真相在服务器；客户端按复制下来的边沿驱动表现） */
UENUM(BlueprintType)
enum class EAsceActionState : uint8
{
	Idle    UMETA(DisplayName = "待机"),
	Move    UMETA(DisplayName = "移动"),
	Attack  UMETA(DisplayName = "攻击"),
	Cast    UMETA(DisplayName = "普通施法（前摇）"),
	Channel UMETA(DisplayName = "持续施法"),
	Dying   UMETA(DisplayName = "死亡中"),
};

/** 施法方式 */
UENUM(BlueprintType)
enum class EAsceCastKind : uint8
{
	Instant UMETA(DisplayName = "瞬发（无前摇：开始即真正释放）"),
	Cast    UMETA(DisplayName = "普通施法（有前摇：前摇结束才真正释放）"),
	Channel UMETA(DisplayName = "持续施法（无前摇：周期结算）"),
};

/** 目标类型（由技能定义决定；客户端不判断，服务器按它挑用 Pos 还是 NetID） */
UENUM(BlueprintType)
enum class EAsceTargetKind : uint8
{
	None  UMETA(DisplayName = "无目标（自身/周围）"),
	Point UMETA(DisplayName = "非指向（地面点）"),
	Unit  UMETA(DisplayName = "指向（单位）"),
};

/** 生效方式 */
UENUM(BlueprintType)
enum class EAsceDelivery : uint8
{
	Instant       UMETA(DisplayName = "直接生效"),
	Projectile    UMETA(DisplayName = "弹道（施法者前方生成）"),
	SpawnAtTarget UMETA(DisplayName = "在目标位置生成"),
};

/** 距离口径（与攻击共用）：近战=表面距离；远程=低→高用表面距离、高→低用圆心距离 */
UENUM(BlueprintType)
enum class EAsceRangeKind : uint8
{
	MeleeSurface     UMETA(DisplayName = "表面距离（近战）"),
	RangedHighGround UMETA(DisplayName = "远程（低→高表面，高→低圆心）"),
};

/** 技能类型 */
UENUM(BlueprintType)
enum class EAsceSkillType : uint8
{
	Active  UMETA(DisplayName = "主动"),
	Passive UMETA(DisplayName = "被动（v1 只做常驻属性加成）"),
};

/** 动画段（客户端 VAT 查表：`(ActionId, ActionState)` → 本段）；服务器不用动画资产 */
USTRUCT(BlueprintType)
struct FAsceAnimSegment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Anim")
	int32 StartFrame = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Anim")
	int32 EndFrame = 0;

	/** 循环（Idle/Move/Channel = true；Attack/Cast 一次性 = false） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Anim")
	bool bLoop = false;

	/** 基准动画速度（实际 PlayRate 由服务器下发；这里是"1.0x"对应的值） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Anim")
	float PlayRate = 1.f;
};

/** 动作定义（攻击也是本表里的一行） */
USTRUCT(BlueprintType)
struct FAsceActionDef : public FTableRowBase
{
	GENERATED_BODY()

	/** 动作 id（写进 fragment / 复制给客户端做动画查表；建议与行名一致） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	int32 ActionId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	EAsceSkillType SkillType = EAsceSkillType::Active;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	EAsceCastKind CastKind = EAsceCastKind::Cast;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	EAsceTargetKind TargetKind = EAsceTargetKind::Unit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	EAsceDelivery Delivery = EAsceDelivery::Instant;

	/** 施法/攻击距离（cm）；距离口径见 `RangeKind` */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	float Range = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	EAsceRangeKind RangeKind = EAsceRangeKind::MeleeSurface;

	/** 前摇时长（= 动作自然时长，秒）；`Instant/Channel` 为 0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	float CastTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	float ChannelDuration = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	float ChannelTickInterval = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	float Cooldown = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	float ManaCost = 0.f;

	/** 伤害（v1 先用一个标量；护甲/类型后续再加） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	float Damage = 0.f;

	/** 弹道（`Delivery == Projectile`）：生成物 = **实体定义表**的行名（见 `UAsceEntityFactory`） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	FName ProjectileDefRow;

	/** 生成物（`Delivery == SpawnAtTarget`）：实体定义表行名 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	FName SpawnGroupDefRow;

	/** 备用：数值型定义 id（暂未使用；行名方式够用就不做 id 查表了） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	int32 ProjectileDefId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	int32 SpawnGroupId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	FAsceAnimSegment Anim;
};
