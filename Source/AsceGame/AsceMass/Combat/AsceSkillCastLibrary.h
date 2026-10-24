// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityHandle.h"

struct FAsceActionDef;
struct FAsceActionFragment;
struct FMassEntityManager;
class UWorld;

/**
 * 施法结算的**可复用 API**（`Combat/技能编辑器设计.md` §10）。
 *
 * 为什么要抽出来：时间轴从 `AsceCombatProcessor` 搬进**行为树**之后，树的 task 和（过渡期的）
 * processor 必须**共用同一份实现**——否则"两边各写一遍、改一边忘另一边"。
 *
 * 约定：**结算永远只在服务器**（客户端只跑表现）；生成物只入队，实体在帧末的生成阶段创建。
 */
namespace AsceSkillCast
{
	/** 造成伤害 + 死亡判定（HP<=0 ⇒ 置 Dying + 停行动 + 入销毁队列）。
	 *  @return 扣血后的 HP；目标无效/没有血条 ⇒ -1（调用方据此判断"没打中"） */
	ASCEGAME_API float ApplyDamage(FMassEntityManager& EntityManager, UWorld* World, FMassEntityHandle Victim, float Amount);

	/** **Trigger 时刻**：扣蓝 + 写槽位 CD（CD 记在槽位上、随槽位换位）。
	 *  @return false = 蓝不够（调用方应放弃本次释放：回到 Idle、不结算、不生成） */
	ASCEGAME_API bool PayCostAndCooldown(FMassEntityManager& EntityManager, FMassEntityHandle Caster
		, int32 ActionId, const FAsceActionDef& Def, double Now);

	/** 结算**一次** effect：`Instant` ⇒ 直接伤害；`Projectile` / `SpawnAtTarget` ⇒ 生成物入队（帧末统一创建）。
	 *  目标失效（指向技能且目标没了）⇒ 什么都不做（不生成、不补刀）。 */
	ASCEGAME_API void ApplyEffect(FMassEntityManager& EntityManager, UWorld* World, FMassEntityHandle Caster
		, FMassEntityHandle Target, const FVector2D& TargetPos, float FacingYaw, int32 CasterTeamId, const FAsceActionDef& Def);

	/** **按实体的当前动作结算一次**（行为树的结算节点用；技能/目标/朝向全从 `FAsceActionFragment` 读）。
	 *  - `bPayCost=true` = Trigger 节点（先扣蓝 + 写槽位 CD，再结算）；`false` = 持续施法的周期节点（只结算）；
	 *  - **只在服务器生效**（客户端直接返回 true，让表现侧的树照常推进）；
	 *  @return false = 蓝不够 / 技能表查不到 ⇒ 调用方（树）应视为失败 */
	ASCEGAME_API bool SettleFromAction(FMassEntityManager& EntityManager, UWorld* World, FMassEntityHandle Caster
		, const FAsceActionFragment& Action, int32 CasterTeamId, bool bPayCost);
}
