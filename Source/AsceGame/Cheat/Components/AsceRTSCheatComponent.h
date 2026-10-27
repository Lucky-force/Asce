// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Cheat/Actions/RTS/AsceRTSCheatAction.h"
#include "Cheat/Components/AsceCheatComponentBase.h"

#include "AsceRTSCheatComponent.generated.h"

/**
 * **RTS 模式的作弊组件**（由 RTS 的 GameFeature 用 `UGameFeatureAction_AddComponents` 挂到 `AModularPlayerState`）。
 *
 * 只做两件事：告诉公共基类"本模式的动作基类"和"本模式的函数库入口"（`UAsceRTSCheatLibrary`）。
 * 别的模式要作弊时，仿照它再写一个 `UXxxCheatComponent` + 一个 `UXxxCheatLibrary` 即可，互不影响。
 *
 * 见 `Cheat/作弊系统设计.md`。
 */
UCLASS()
class ASCEGAME_API UAsceRTSCheatComponent : public UAsceCheatComponentBase
{
	GENERATED_BODY()

public:
	virtual TSubclassOf<UAsceCheatAction> GetActionBaseClass() const override { return UAsceRTSCheatAction::StaticClass(); }
	virtual bool ExecuteLine(const FString& Line, FString& OutSummary, FString& OutError) override;
};
