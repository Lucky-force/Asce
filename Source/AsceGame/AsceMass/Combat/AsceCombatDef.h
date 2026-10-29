// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/SoftObjectPtr.h"

#include "AsceCombatDef.generated.h"

class UAnimSequence;
class UStateTree;

/**
 * 动作 / 技能定义（数据表行；服务器与客户端共用同一份）。
 *
 * 见 `技能系统设计.md`：
 *  - **攻击 = 内置动作定义**（自动触发、无蓝耗；配置在单位 def 的 `AttackCastConfig`），技能 = 玩家触发的动作定义；
 *    两者共用同一套"占时 → Trigger 结算 → 打断"管线。
 *  - "**真正释放（Trigger）**" = 处理 cost + CD + effect 的那一帧：普通 = `TriggerTime`；持续 = 前摇结束。
 */

/** 动作状态（唯一真相在服务器；客户端按复制下来的边沿驱动表现） */
UENUM(BlueprintType)
enum class EAsceActionState : uint8
{
	Idle    UMETA(DisplayName = "待机"),
	Move    UMETA(DisplayName = "移动"),
	Attack  UMETA(DisplayName = "攻击"),
	// 不能叫 `Cast`：`cast` 是 AngelScript 保留字，注册到 AS 会报 asINVALID_NAME
	Casting UMETA(DisplayName = "普通施法（前摇）"),
	Channel UMETA(DisplayName = "持续施法"),
	Dying   UMETA(DisplayName = "死亡中"),
};

/** 目标关系（仅 `TargetKind == Unit` 时校验）：Enemy = 非同队（含中立）；Friendly = 同队（含自己）；Self = 仅自己 */
UENUM(BlueprintType)
enum class EAsceTargetRelation : uint8
{
	Enemy    UMETA(DisplayName = "敌方"),
	Friendly UMETA(DisplayName = "友方"),
	Self     UMETA(DisplayName = "自身"),
	Any      UMETA(DisplayName = "任意"),
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

/**
 * **释放方式**（取代原来的"主动/被动"）：
 *  - `Manual` = 玩家指令释放（技能）；
 *  - `Auto`   = 由系统自动触发（**普通攻击**：战斗扫描选中目标后自己起手）。
 *  "被动加成"不是一个动作 ⇒ v2 用单位 def 上的 modifier 列表做，不占动作表。
 */
UENUM(BlueprintType)
enum class EAsceReleaseMode : uint8
{
	Manual UMETA(DisplayName = "手动（技能）"),
	Auto   UMETA(DisplayName = "自动（普通攻击）"),
};

/**
 * 动作管线配置（**多态基类**，技能表里用 `FInstancedStruct` 存）。
 *
 * **服务器只取时间，客户端只取动画名**——同一份配置两端共读：
 *   - 普通施法：`TotalCastTime` = 动作总时长（动画尾巴）；`TriggerTime` = 真正结算时刻（可 < 总时长）。
 *   - 持续施法：`CastTime` 前摇 → 前摇结束=真正释放 → 进入 Channel（循环动画 + 每 TickInterval 结算）→ ChannelDuration。
 * 见 `技能系统设计.md` §2.1 / §5。
 */
USTRUCT(BlueprintType)
struct FAsceSkillCastConfig
{
	GENERATED_BODY()

	virtual ~FAsceSkillCastConfig() = default;

	/** 是否持续施法 */
	virtual bool IsChannel() const { return false; }
	/** 前摇/施法动画资产（空 = 该状态没有动画，不播） */
	virtual TSoftObjectPtr<UAnimSequence> GetCastAnim() const { return TSoftObjectPtr<UAnimSequence>(); }
	/** 持续施法动画资产（空 = 该状态没有动画，不播） */
	virtual TSoftObjectPtr<UAnimSequence> GetChannelAnim() const { return TSoftObjectPtr<UAnimSequence>(); }
	/** 真正结算时刻（相对动作开始，秒） */
	virtual float GetTriggerTime() const { return 0.f; }
	/** 动作总时长（普通 = 动画尾巴；持续 = 前摇时长，秒） */
	virtual float GetTotalCastTime() const { return 0.f; }
	/** 持续施法总时长（秒） */
	virtual float GetChannelDuration() const { return 0.f; }
	/** 持续结算间隔（秒） */
	virtual float GetTickInterval() const { return 0.5f; }
};

/** 普通施法：`TriggerTime` 结算 → 动作播到 `TotalCastTime` → Idle */
USTRUCT(BlueprintType)
struct FAsceNormalCastConfig : public FAsceSkillCastConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	TSoftObjectPtr<UAnimSequence> CastAnim;

	/** 动作总时长（动画尾巴，秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	float TotalCastTime = 1.f;

	/** 真正结算时刻（可 < TotalCastTime：动画播到某个位置就触发；**各算各的，到点硬切**） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	float TriggerTime = 1.f;

	virtual TSoftObjectPtr<UAnimSequence> GetCastAnim() const override { return CastAnim; }
	virtual float GetTriggerTime() const override { return TriggerTime; }
	virtual float GetTotalCastTime() const override { return TotalCastTime; }
};

/** 持续施法：前摇 `CastTime` → 前摇结束=真正释放 → Channel 循环动画 + 周期结算 → `ChannelDuration` */
USTRUCT(BlueprintType)
struct FAsceChannelCastConfig : public FAsceSkillCastConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	TSoftObjectPtr<UAnimSequence> CastAnim;

	/** 前摇时长（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	float CastTime = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	TSoftObjectPtr<UAnimSequence> ChannelAnim;

	/** 持续施法总时长（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	float ChannelDuration = 3.f;

	/** 周期结算间隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	float TickInterval = 0.5f;

	virtual bool IsChannel() const override { return true; }
	virtual TSoftObjectPtr<UAnimSequence> GetCastAnim() const override { return CastAnim; }
	virtual TSoftObjectPtr<UAnimSequence> GetChannelAnim() const override { return ChannelAnim; }
	virtual float GetTriggerTime() const override { return CastTime; }
	virtual float GetTotalCastTime() const override { return CastTime; }
	virtual float GetChannelDuration() const override { return ChannelDuration; }
	virtual float GetTickInterval() const override { return TickInterval; }
};

/** 动作定义（技能行；**攻击不入本表**——攻击配置在单位 def 的 `AttackCastConfig`） */
USTRUCT(BlueprintType)
struct FAsceActionDef : public FTableRowBase
{
	GENERATED_BODY()

	/** 动作 id（写进 fragment / 复制给客户端做动画查表；建议与行名一致） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	int32 ActionId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	EAsceReleaseMode ReleaseMode = EAsceReleaseMode::Manual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	EAsceTargetKind TargetKind = EAsceTargetKind::Unit;

	/** 目标关系（仅 Unit 指向时校验；None = 自身/周围，Point 不校验） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	EAsceTargetRelation TargetRelation = EAsceTargetRelation::Enemy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	EAsceDelivery Delivery = EAsceDelivery::Instant;

	/** 施法/攻击距离（cm）；距离口径见 `RangeKind` */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	float Range = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	EAsceRangeKind RangeKind = EAsceRangeKind::MeleeSurface;

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

	/** 动作管线配置（多态：普通 / 持续）——**注意：动作行不再带 CastConfig**（技能的时间轴/动画在技能子树里，
	 *  攻击的时间线在单位 def 的旧 `AttackCastConfig` 兜底）。此字段已删除。 */
	// FInstancedStruct CastConfig;  ← 已删除

	/** **技能子树**（Mass StateTree schema）：`Casting` 状态里的 `Asce Run Ability Tree` 按本行 `ActionId` 选它来跑。
	 *  技能树可被任意单位类型复用（自由组合），不参与 archetype。空 = 该技能不可释放（服务器校验会拒绝）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Action")
	TObjectPtr<UStateTree> SkillTree = nullptr;
};
