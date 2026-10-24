// AsceUnitDef.h
#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "AsceMass/Data/AsceEntityDefTypes.h"
#include "DataTableSystem/AsceRowNameWrapper.h"
#include "AsceUnitDef.generated.h"

class UAsceAnimSet;
class UAnimSequence;

/**
 * RTS 单位类型定义（实体定义表行 `Def` 的一种）：只放"同一结构、数值不同"的每类型数据。
 *
 * - **结构**（fragment/tag 组合、能力开关）由 trait 资产决定；
 * - 本结构的值由 `UAsceEntityFactory::SpawnEntitiesByDef` 在生成时写进对应 fragment；
 * - 以后模型/攻击模式等表现数据继续往这里加。
 */
USTRUCT(BlueprintType)
struct FAsceRTSUnitDef : public FAsceEntityDefBase
{
	GENERATED_BODY()

	// ── 移动 ──

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	float MaxSpeed = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	float MaxAcceleration = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	float DefaultDesiredSpeed = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	float Radius = 75.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	float Height = 150.f;

	// ── 视野 ──

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vision")
	int32 SightRadius = 20;

	// ── 属性 ──

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
	float MaxMana = 100.f;

	// ── 战斗（写入 FAsceCombatFragment；见 `Combat/技能系统设计.md`）──

	/** 攻击距离（cm）；v1 固定"近战表面/圆心"口径，高度口径后补 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
	float AttackRange = 300.f;

	/** 攻击力（技能/攻击的伤害基数；动作行的 `Damage` 为 0 时用它） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
	float AttackDamage = 10.f;

	/** 攻击的动作管线配置（NormalCast）：时间 + **攻击动画 `CastAnim`**（1.0x 配，运行时由 AttackSpeed 缩放；空 = 默认 Total=1s / Trigger=1s、无攻击动画） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (BaseStruct = "/Script/AsceGame.AsceNormalCastConfig"))
	FInstancedStruct AttackCastConfig;

	/** 攻击速度；PlayRate = AttackSpeed / 200 + 0.1（180 → 1.0x） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
	float AttackSpeed = 180.f;

	/** 自动攻击默认开关（玩家可切） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bAutoAttackDefault = true;

	/** **攻击 = 一条 `ReleaseMode = Auto` 的动作行**（动作表里的行名）。
	 *  行里决定：时间轴（CastConfig，含**攻击动画 `CastAnim`**）、交付方式（Instant/Projectile）、
	 *  `Range/Damage` 为 0 时跟随下面的**基础属性**。留空 = 退回旧的 `AttackCastConfig` 路径（迁移用）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat",
		meta = (DataTableBinding = "AsceActionDefRow:ActionId"))
	FAsceRowNameWrapper AttackAction;

	// ── 技能（写入 FAsceSkillSlotsFragment 的初始槽位；见 `Combat/技能系统设计.md`）──

	/** 初始技能槽位（1/2/3 ↔ Q/W/E）：每项是技能（动作定义）id；0 = 空槽。
	 *  运行时换槽由"配置槽位"指令改，这里只是出生时的初始配置 */
	// ★ **固定 3 个**（与运行时 `FAsceSkillSlotsFragment::Slots[3]` 一致），元素是**动作表行名** =
	//   下拉选技能；空 = 空槽。旧版是 `TArray<int32>`（可以任意加、裸 id）⇒ 已迁移，内容需要重填一次。
	// ⚠️ 静态 C 数组**不能暴露给蓝图**（UHT 限制）⇒ 只给 EditAnywhere；C++ 侧（工厂）照常读
	UPROPERTY(EditAnywhere, Category = "Skill",
		meta = (DataTableBinding = "AsceActionDefRow:ActionId"))
	FAsceRowNameWrapper SkillSlots[3];

	/** 该单位类型的动画集（Bone 模式；客户端通过复制的 UniqueTypeId 查本行解析动画）。见 `Combat/AsceAnimDef.h` */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAsceAnimSet> AnimSet;

	// ── 动画语义（AnimSet 只是"资产 → 帧段"的库；哪个状态播哪条动画由本行决定）──
	// 规则：**没配就不播**（不再有保留名/回退链）。Idle/Move/Dying 用下面三个槽位；
	// **攻击动画 = `AttackCastConfig.CastAnim`**（动画统一跟 CastConfig 走，不再单列 AttackAnim）。

	/** 待机（循环） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> IdleAnim;

	/** 移动（循环） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> MoveAnim;

	/** 死亡（一次性） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> DyingAnim;

	// ── 动画变体（可选；空 = 用上面的单条）──

	/** 待机变体：>1 条时按**选择种子**随机（种子由行为树的 `Asce Select Anim Variant` task 写；
	 *  没挂那个 task 时种子=0 ⇒ 固定第一条）。见 `FAsceAnimPlaybackFragment::VariantSeed` */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TArray<TSoftObjectPtr<UAnimSequence>> IdleAnimVariants;

	/** 移动变体：**下一步接**"按速度选"；留空 = 用 `MoveAnim` */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TArray<TSoftObjectPtr<UAnimSequence>> MoveAnimVariants;

	/** 达到该速度（cm/s）时用 `MoveAnimVariants[1]`（走/跑分档）；<=0 = 不分档（配合 `MoveAnimVariants` 使用） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	float MoveAnimRunSpeedCm = 0.f;
};
