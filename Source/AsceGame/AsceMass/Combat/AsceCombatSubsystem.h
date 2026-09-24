// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Subsystems/WorldSubsystem.h"

#include "AsceCombatSubsystem.generated.h"

class UMassProcessor;

/**
 * 战斗阶段（**服务器权威**）：持有并驱动 `UAsceCombatProcessor`。
 *
 * - 不自己 Tick 干活：由 `UAsceSimulationSubsystem` 的阶段链按**硬顺序**调用
 *   （… → 命令 drain → **战斗** → 雾渲染 → UI 采样）。
 * - 客户端不跑（`RunCombatStage` 里门控），standalone 会跑。
 * - 见 `技能系统设计.md`。
 */
UCLASS()
class UAsceCombatSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** 阶段入口：由 `UAsceSimulationSubsystem` 在"命令 drain 之后、雾渲染之前"调用 */
	void RunCombatStage();

private:
	/** 战斗推进 processor：`NewObject` 出来由本子系统持有（Mass 的 processor 是 UObject，
	 *  没有引用会被 GC；且它设了 bAutoRegisterWithProcessingPhases=false，不会被 Mass 全局调度驱动） */
	UPROPERTY(Transient)
	TObjectPtr<UMassProcessor> CombatProcessor;
};
