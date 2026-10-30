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

	/** 基础移动速度（cm/s）：最大速度 = 2×基础；奔跑阈值 = 1.7×基础 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "1.0"))
	float BaseMoveSpeed = 150.f;


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

	// ── 战斗（写入 FAsceCombatAttributesFragment；见 `Combat/技能系统设计.md`）──

	/** 攻击距离（cm）；v1 固定"近战表面/圆心"口径，高度口径后补 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
	float AttackRange = 300.f;

	/** 攻击力（技能/攻击的伤害基数；动作行的 `Damage` 为 0 时用它） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
	float AttackDamage = 10.f;

	/** 攻击速度（基础属性；动画播放速率/节奏由行为树 cue/节点决定） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
	float AttackSpeed = 180.f;

	/** **攻击 = 一条 `ReleaseMode = Auto` 的动作行**（动作表里的行名）。
	 *  行里的 `SkillTree` 是攻击子树（时间轴/结算/动画都在树里）；`Range/Damage` 为 0 时跟随基础属性。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat",
		meta = (DataTableBinding = "AsceActionDefRow:ActionId"))
	FAsceRowNameWrapper AttackAction;

	// ── 技能（写入 FAsceSkillSlotsFragment 的初始槽位；见 `Combat/技能系统设计.md`）──

	/** 初始技能槽位（1/2/3 ↔ Q/W/E）：每项是技能（动作定义）行名；空 = 空槽。
	 *  运行时换槽由"配置槽位"指令改，这里只是出生时的初始配置 */
	// ★ **固定 3 个**（与运行时 `FAsceSkillSlotsFragment::Slots[3]` 一致）；静态 C 数组不能暴露给蓝图
	UPROPERTY(EditAnywhere, Category = "Skill",
		meta = (DataTableBinding = "AsceActionDefRow:ActionId"))
	FAsceRowNameWrapper SkillSlots[3];

	/** 该单位类型的动画集（cue task 用它把动画资产解析成帧段；见 `Combat/AsceAnimDef.h`） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAsceAnimSet> AnimSet;

	// ── 状态 → 动画（**手配数据**，不走烘焙；cue 任务用 Source=DefVariants 按状态读） ──
	// 数组 = 变体，播放时随机选一个；帧段仍由 AnimSet.Clips 查（序列 → 烘焙帧）。

	/** 待机 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State")
	TArray<TSoftObjectPtr<UAnimSequence>> IdleAnims;

	/** 待机小动作（IdleBreak）：Idle 状态里每隔一段随机时间播一个 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State")
	TArray<TSoftObjectPtr<UAnimSequence>> IdleBreakAnims;

	/** 普通移动 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State")
	TArray<TSoftObjectPtr<UAnimSequence>> MoveAnims;

	/** 快速奔跑 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State")
	TArray<TSoftObjectPtr<UAnimSequence>> RunAnims;

	/** 受伤后移动 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State")
	TArray<TSoftObjectPtr<UAnimSequence>> InjuredMoveAnims;

	/** 攻击（每次从数组里随机选一个） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State")
	TArray<TSoftObjectPtr<UAnimSequence>> AttackAnims;

	/** 死亡 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State")
	TArray<TSoftObjectPtr<UAnimSequence>> DyingAnims;

	/** 被控制（预留） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State")
	TArray<TSoftObjectPtr<UAnimSequence>> ControlledAnims;

	/** 眩晕（预留） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State")
	TArray<TSoftObjectPtr<UAnimSequence>> StunnedAnims;
};
