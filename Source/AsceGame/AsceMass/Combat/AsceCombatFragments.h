// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassEntityTypes.h"
#include "MassEntityHandle.h"   // FMassEntityHandle（5.7 起 MassEntityTypes.h 不再带它）

#include "AsceCombatDef.h"

#include "AsceCombatFragments.generated.h"

/**
 * 战斗 / 动作 / 技能 fragment（见 `技能系统设计.md`）。
 *
 * 分工：
 *  - `FAsceCombatFragment`：**服务器权威**的攻击参数与运行时（不复制；挂载 = 该实体能主动攻击）。
 *  - `FAsceActionFragment`：**服务器权威**的"当前动作"（唯一真相；客户端靠动作态通道复制下来的边沿驱动表现）。
 *  - `FAsceSkillSlotsFragment`：槽位（**真实数据，要复制到客户端**；技能的 CD 等运行时状态随槽位）。
 *  - `FAsceManaFragment`：蓝量（走标量复制通道给 UI 显示）。
 */

/** 攻击能力（服务器权威，不复制）。挂了这个 fragment = 能主动攻击；不挂 = 只能被打。 */
USTRUCT()
struct FAsceCombatFragment : public FMassFragment
{
	GENERATED_BODY()

	// ── 静态（生成时从 def 写死）──
	float AttackRange = 0.f;                 // cm
	float AttackDamage = 0.f;
	float BaseAttackTime = 1.f;              // 攻击动作自然时长（秒）
	float AttackSpeed = 180.f;               // PlayRate = AttackSpeed / 200 + 0.1（180 → 1.0x）
	EAsceRangeKind RangeKind = EAsceRangeKind::MeleeSurface;
	bool  bAutoAttack = true;                // 自动攻击开关（玩家可切；`S` 停止指令不影响它）
	bool  bProjectile = false;               // 远程弹道（false = 即时命中）
	int32 ProjectileDefId = 0;

	// ── 运行时（服务器专有，绝不复制）──
	FMassEntityHandle DesignatedTarget;      // 指定目标（持久：目标不死/不取消就一直记，且范围内优先）
	float NextScanTime = 0.f;                // 0.3s 节流扫描
};

/** 当前动作（服务器权威；同一时刻只有一个动作） */
USTRUCT()
struct FAsceActionFragment : public FMassFragment
{
	GENERATED_BODY()

	EAsceActionState ActionState = EAsceActionState::Idle;

	/** 动作定义 id：攻击 = 攻击用的 ActionId；技能 = 槽位里的技能 ActionId（客户端据此查动画段/时长） */
	int32 ActionDefId = 0;

	/** 动作结束时刻（服务器世界时间）；`Channel` 为持续施法结束时刻 */
	double ActionEndTime = 0.0;

	/** 攻击 CD 结束时刻（技能的 CD 记在槽位上，见 `FAsceSkillSlot::NextReadyServerTime`） */
	double NextReadyTime = 0.0;

	/** `Channel` 的周期结算时刻 */
	double NextTickTime = 0.0;

	/** 本次动作的目标实体（可无效：非指向技能/无目标动作） */
	FMassEntityHandle ActionTarget;

	/** 非指向技能的目标点（TargetKind == Point 时用） */
	FVector2D ActionTargetPos = FVector2D::ZeroVector;

	/** 本次动作的朝向（动作开始时定死；动作期间不变） */
	float FacingYaw = 0.f;

	/** 递增序号：每次"下发一次动作"++（客户端看到变了就播一次；也用于区分连续两次攻击） */
	uint32 Counter = 0;
};

/** 技能槽位（真实数据：指令只发槽位号，服务器按它解析技能） */
USTRUCT(BlueprintType)
struct FAsceSkillSlot
{
	GENERATED_BODY()

	/** 技能（动作定义）id；0 = 空槽 */
	UPROPERTY(BlueprintReadWrite, Category = "Asce|Skill")
	int32 ActionId = 0;

	/** 下次可释放的**服务器世界时间**（CD）；客户端用 `GetServerWorldTimeSeconds()` 对比做倒计时 */
	UPROPERTY(BlueprintReadWrite, Category = "Asce|Skill")
	double NextReadyServerTime = 0.0;
};

/** 3 个技能槽位（1/2/3 ↔ Q/W/E）；技能运行时状态随槽位（换槽 = 技能连同 CD 一起换位） */
USTRUCT()
struct FAsceSkillSlotsFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 槽位数量（1/2/3 ↔ Q/W/E） */
	static constexpr int32 NumSkillSlots = 3;

	FAsceSkillSlot Slots[NumSkillSlots];
};

/** 蓝量（技能消耗）；复制走标量通道（`ManaRatio`），与 Health 同款 */
USTRUCT()
struct FAsceManaFragment : public FMassFragment
{
	GENERATED_BODY()

	float MaxMana = 0.f;
	float CurrentMana = 0.f;
};
