// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"

#include "AsceCombatProcessor.generated.h"

/**
 * 服务器战斗推进（**只在权威端跑**，见 `技能系统设计.md` §3/§4/§5）：
 *
 *  ① **状态驱动**：`Idle ↔ Move` 由移动命令切换；`Attack` 由移动打断（只取消状态，伤害/表现交给动作行子树）。
 *  ② **扫描起手**：`Idle` + `FAsceCombatAttributesFragment` 存在 + **当前没有移动命令** + 0.3s 节流 →
 *     优先打"指定目标"，否则找**射程内最近的敌方**（视线 = 迷雾可见）→ 进入 `Attack`
 *     （起手时刻写**朝向** + `ActionDefId`（= 攻击动作行）+ 目标 + `StartServerTime` 锚点 + `Counter++`）。
 *
 * 动作本身的时长/动画/结算时刻全部由**动作行的 `SkillTree`** 决定（`Asce Run Ability Tree` 按 `ActionId` 跑），
 * 本 processor 不碰时间线。
 *
 * 本 processor **不负责**：死亡/移除、技能施法细节（树负责）、以及任何复制（表现走动作态通道）。
 *
 * v1 简化（后续步骤补）：
 *  - 距离口径先用"圆心距离 ≤ Range + 双方半径"（表面距离/高地口径在引入高度数据后替换）；
 *  - 伤害直接写 `FAsceHealthFragment`（护甲/类型/飘字后续）；
 *  - 不处理死亡（血量 ≤ 0 的移除/死亡动作后续）。
 */
UCLASS()
class UAsceCombatProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceCombatProcessor();

	/** 攻击的 ActionDefId（v1 先用 0；后续改为"攻击动作表行"的 id） */
	static constexpr int32 AttackActionId = 0;

	/** 扫描节流间隔（秒） */
	static constexpr float ScanIntervalSeconds = 0.3f;

	//~UMassProcessor
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
	//~End of UMassProcessor

private:
	FMassEntityQuery EntityQuery;
};
