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
 *  - `FAsceCombatAttributesFragment`：攻击**属性**（攻击距离/伤害/攻速/远程近战/攻击动作行 id），
 *    走通用值复制通道下发到客户端（客户端要拿它算攻击动画 PlayRate、画攻击范围）；
 *    挂载 = 该实体能主动攻击。
 *  - `FAsceCombatFragment`：**运行时**（服务器权威，不复制）：指定目标 / 扫描节流。
 *  - `FAsceActionFragment`：**服务器权威**的"当前动作"（唯一真相；客户端靠动作态通道复制下来的边沿驱动表现）。
 *  - `FAsceCombatVisualFragment`：**客户端表现**（动作态复制通道写入；驱动转身/VAT）。
 *  - `FAsceSkillSlotsFragment`：槽位（**真实数据，要复制到客户端**；技能的 CD 等运行时状态随槽位）。
 *  - `FAsceManaFragment`：蓝量（走通用值复制通道给 UI 显示）。
 *
 * 字段统一加 `UPROPERTY`（反射用：UI 采样器/inspector/调试可视化；这些 fragment 本身不参与网络复制）。
 */

/** 攻击属性（生成时从 def 写死；走值复制通道给客户端）。挂了这个 fragment = 能主动攻击；不挂 = 只能被打。 */
USTRUCT()
struct FAsceCombatAttributesFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY()
	float AttackRange = 0.f;                 // cm
	UPROPERTY()
	float AttackDamage = 0.f;
	UPROPERTY()
	float AttackSpeed = 180.f;               // PlayRate = AttackSpeed / 200 + 0.1（180 → 1.0x）
	UPROPERTY()
	EAsceRangeKind RangeKind = EAsceRangeKind::MeleeSurface;

	/** 攻击用的**动作行 id**（`FAsceActionDef::ActionId`；0 = 没配攻击行） */
	UPROPERTY()
	int32 AttackActionId = 0;
};

/** 战斗运行时状态（服务器权威，不复制） */
USTRUCT()
struct FAsceCombatFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY()
	FMassEntityHandle DesignatedTarget;      // 指定目标（持久：目标不死/不取消就一直记，且范围内优先）
	UPROPERTY()
	float NextScanTime = 0.f;                // 0.3s 节流扫描
};

/** 当前动作（服务器权威；同一时刻只有一个动作） */
USTRUCT()
struct FAsceActionFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY()
	EAsceActionState ActionState = EAsceActionState::Idle;

	/** 动作定义 id：攻击 = 攻击用的 ActionId；技能 = 槽位里的技能 ActionId（客户端据此查动画段/时长） */
	UPROPERTY()
	int32 ActionDefId = 0;

	/** 真正结算时刻（服务器世界时间；普通=TriggerTime；持续=前摇结束进 Channel 那一刻） */
	UPROPERTY()
	double TriggerTime = 0.0;

	/** 本次动作是否已结算（防重复；服务器运行时） */
	UPROPERTY()
	bool bTriggered = false;

	/** 动作结束时刻（服务器世界时间）；`Channel` 为持续施法结束时刻 */
	UPROPERTY()
	double ActionEndTime = 0.0;

	/** 攻击 CD 结束时刻（技能的 CD 记在槽位上，见 `FAsceSkillSlot::NextReadyServerTime`） */
	UPROPERTY()
	double NextReadyTime = 0.0;

	/** `Channel` 的周期结算时刻 */
	UPROPERTY()
	double NextTickTime = 0.0;

	/** 本次动作的目标实体（可无效：非指向技能/无目标动作） */
	UPROPERTY()
	FMassEntityHandle ActionTarget;

	/** 非指向技能的目标点（TargetKind == Point 时用） */
	UPROPERTY()
	FVector2D ActionTargetPos = FVector2D::ZeroVector;

	/** 本次动作的朝向（动作开始时定死；动作期间不变） */
	UPROPERTY()
	float FacingYaw = 0.f;

	/** 递增序号：每次"下发一次动作"++（客户端看到变了就播一次；也用于区分连续两次攻击） */
	UPROPERTY()
	uint32 Counter = 0;

	/** 本次动作的起轴时刻（服务器世界时间；动作开始时写；随动作态通道下发） */
	UPROPERTY()
	float StartServerTime = 0.f;
};

/**
 * **客户端表现**：动作态（由"动作态复制通道"写入；服务器保持默认 Idle，不读）。
 *
 * 用途：
 *  - 客户端转身：`UAsceMovementOrientationProcessor` 在 Attack/Casting/Channel 时按
 *    `FacingYaw` 转向（速度朝向让位）；
 *  - 后续 VAT 动画（`技能系统设计.md` §9.1 ③）：按 `(ActionDefId, ActionState)` 查动画段 +
 *    `PlayRate`，`Counter` 变化时"播一次"。
 *
 * 只在单位模板上挂（见 `UAsceRTSUnitTrait::BuildTemplate`）。
 */
USTRUCT()
struct FAsceCombatVisualFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY()
	EAsceActionState ActionState = EAsceActionState::Idle;

	/** 动作定义 id（攻击 = 0；技能 = 槽位里的技能 id） */
	UPROPERTY()
	int32 ActionDefId = 0;

	/** 递增序号（变了 = 下发了一次新动作） */
	UPROPERTY()
	uint32 Counter = 0;

	/** 播放速度（服务器按攻击速度量化下发；技能 v1 = 1.0） */
	UPROPERTY()
	float PlayRate = 1.f;

	/** 本次动作朝向（度；动作开始定死，动作期间不变） */
	UPROPERTY()
	float FacingYaw = 0.f;

	/** 本次动作的起轴时刻（回绕百分秒；与 FAsceActionFragment::StartServerTime 同一编码）
	 *  客户端据此把时间轴标定到**服务器的**起点（见 Combat/技能编辑器设计.md §10.3） */
	UPROPERTY()
	uint16 ActionStartTimeQ = 0;
};

/**
 * **客户端/本地**动画播放状态（表现 processor 维护；服务器/客户端都可能有，不复制）。
 *
 * 动作边沿（`Counter`/`ActionState` 变化）时解析一次动画（状态 → 动画资产 → AnimSet 的 `Clips`；没配就不播），
 * 之后每帧按 `StartTime/FrameRate/PlayRate` 推进帧，写进 ISM per-instance custom data。
 */
USTRUCT()
struct FAsceAnimPlaybackFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 解析结果（边沿时重算） */
	UPROPERTY()
	int32 AnimIndex = 0;
	UPROPERTY()
	int32 StartFrame = 0;
	UPROPERTY()
	int32 EndFrame = 0;
	UPROPERTY()
	float FrameRate = 30.f;
	UPROPERTY()
	float PlayRate = 1.f;
	UPROPERTY()
	bool bLoop = false;
	/** false = 没有可播的动画（写中立值） */
	UPROPERTY()
	bool bValid = false;

	/** 本次动作开始的世界时间（本地时钟） */
	UPROPERTY()
	float StartTime = 0.f;

	/** ── 表现 cue（**表现层/逻辑层分层**：由行为树的 `Asce Play Anim Cue` task 在客户端**解析好帧段**后写入）──
	 *  Mass fragment 必须 trivially copyable ⇒ cue 里**不放资产引用**，只放 AnimSet 内绝对帧；
	 *  `CueCounter` 变 ⇒ 重播。`bCueActive=false` = 该实体还没有 cue（不播）。 */
	UPROPERTY()
	uint32 CueCounter = 0;

	/** 表现 processor 侧记录：上次已消费的 cue 序号 */
	UPROPERTY()
	uint32 LastCueCounter = 0;

	UPROPERTY()
	bool bCueActive = false;

	UPROPERTY()
	int32 CueAnimIndex = 0;

	UPROPERTY()
	int32 CueStartFrame = 0;

	UPROPERTY()
	int32 CueEndFrame = 0;

	UPROPERTY()
	float CueFrameRate = 30.f;

	UPROPERTY()
	float CuePlayRate = 1.f;

	UPROPERTY()
	bool bCueLoop = true;
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
	// 【两端语义不同】服务器侧 = 绝对服务器世界时间（CD，0 = 就绪）；
	// 客户端侧 = 复制通道写成的"CD 百分秒回绕值"（0..65535），使用方（UI/预测）按回绕值解释。
	// 见 `Networking/Capabilities/SkillPanel/*`
	UPROPERTY(BlueprintReadWrite, Category = "Asce|Skill")
	double NextReadyServerTime = 0.0;
};

/** 客户端 CD 判定（`技能系统设计.md` §6.2）：客户端侧 `FAsceSkillSlot::NextReadyServerTime` 存的是
 *  **CD 百分秒回绕值**（0 = 未落 CD/就绪），用服务器世界时间对比。回绕安全：`now` 落在 `nextReady`
 * 之后（半圈 = 32768 百分秒 ≈ 5.5 分钟内）即视为就绪。UI 与客户端预测共用这一份。 */
FORCEINLINE bool AsceClientIsSkillCooldownReady(const double ClientNextReadyWrapped, const double ServerWorldTimeSeconds)
{
	if (ClientNextReadyWrapped <= 0.0)
	{
		return true;
	}
	const int32 NextReadyQ = static_cast<int32>(ClientNextReadyWrapped);
	const int32 NowQ = static_cast<int32>(FMath::Fmod(ServerWorldTimeSeconds * 100.0, 65536.0));
	return ((NowQ - NextReadyQ) & 0xFFFF) < 0x8000;
}

/** 3 个技能槽位（1/2/3 ↔ Q/W/E）；技能运行时状态随槽位（换槽 = 技能连同 CD 一起换位） */
USTRUCT()
struct FAsceSkillSlotsFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 槽位数量（1/2/3 ↔ Q/W/E） */
	static constexpr int32 NumSkillSlots = 3;

	FAsceSkillSlot Slots[NumSkillSlots];
};

/** 蓝量（技能消耗）；复制走通用值通道（`MaxMana + CurrentMana` 绝对值），与 Health 同款 */
USTRUCT()
struct FAsceManaFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY()
	float MaxMana = 0.f;
	UPROPERTY()
	float CurrentMana = 0.f;
};
