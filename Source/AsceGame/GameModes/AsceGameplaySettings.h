// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "AsceGameplaySettings.generated.h"

class UStateTree;

/**
 * **运行时玩法默认配置**（`Config/DefaultGame.ini` → `[/Script/AsceGame.AsceGameplaySettings]`）。
 *
 * 目的：让"通用但每类型要配资产"的东西有**全局默认**，单位定义行里不填就回落：
 * - `DefaultUnitStateTree`：单位类型没配 `FAsceRTSUnitDef::StateTree` 时用它（通用单位树）；
 * - `DefaultMeleeAttackTree` / `DefaultRangedAttackTree`：**攻击动作行**没配 `SkillTree` 时，
 *   按动作行的 `RangeKind`（MeleeSurface / RangedHighGround）回落（只对 `ReleaseMode == Auto` 的攻击生效）。
 *
 * 见 `AsceMass/Data/单位模块化设计.md`、`Combat/技能编辑器设计.md`。
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Asce Gameplay"))
class ASCEGAME_API UAsceGameplaySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 通用单位状态树（def 的 StateTree 为空时使用；只对单位类型生效，建筑不挂） */
	UPROPERTY(EditDefaultsOnly, config, Category = "Tree", meta = (AllowedClasses = "/Script/StateTreeModule.StateTree"))
	TSoftObjectPtr<UStateTree> DefaultUnitStateTree;

	/** 近战攻击的默认技能子树（攻击行动作行 SkillTree 为空 + RangeKind == MeleeSurface 时使用） */
	UPROPERTY(EditDefaultsOnly, config, Category = "Tree", meta = (AllowedClasses = "/Script/StateTreeModule.StateTree"))
	TSoftObjectPtr<UStateTree> DefaultMeleeAttackTree;

	/** 远程攻击的默认技能子树（攻击行动作行 SkillTree 为空 + RangeKind == RangedHighGround 时使用） */
	UPROPERTY(EditDefaultsOnly, config, Category = "Tree", meta = (AllowedClasses = "/Script/StateTreeModule.StateTree"))
	TSoftObjectPtr<UStateTree> DefaultRangedAttackTree;

	virtual FName GetCategoryName() const override { return FName(TEXT("Asce")); }
};
