// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"

#include "AsceCombatProcessor.generated.h"

/**
 * 服务器战斗推进（**只在权威端跑**，见 `技能系统设计.md` §3/§4/§5）：
 *
 *  ① **动作推进**：`Attack` 在 `TriggerTime` **结算一次**（结算时要求目标仍有效、在自己 2× 射程内；
 *     否则只结束动作不产生伤害）→ 播完动画尾巴回 `Idle`、落 CD（`NextReadyTime = now + 1/PlayRate`）。
 *  ② **扫描起手**：`Idle` + 自动攻击开 + CD 到 + **当前没有移动命令** + 0.3s 节流 →
 *     优先打"指定目标"，否则找**射程内最近的敌方**（视线 = 迷雾可见）→ 进入 `Attack`
 *     （动作开始时**对齐朝向**、`TriggerTime/ActionEndTime` 由单位 `AttackCastConfig` 定、`Counter++`）。
 *
 * 本 processor **不负责**：移动打断（由移动管线/停止指令负责，见后续步骤）、死亡/移除、
 * 技能施法（后续步骤）、以及任何复制（表现走动作态通道）。
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
