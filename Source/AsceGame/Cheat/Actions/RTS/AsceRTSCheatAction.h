// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Cheat/AsceCheatAction.h"

#include "AsceRTSCheatAction.generated.h"

/**
 * **RTS 模式作弊动作的基类**（只用来做"按模式过滤"）。
 *
 * 通用能力都在 `UAsceCheatAction`；本类不加字段。写 RTS 的新作弊时派生它即可，
 * `UAsceCheatLibrary::GetActions(UAsceRTSCheatAction::StaticClass())` 会自动收进来。
 */
UCLASS(Abstract)
class ASCEGAME_API UAsceRTSCheatAction : public UAsceCheatAction
{
	GENERATED_BODY()
};
