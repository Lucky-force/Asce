// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "AsceRTSCheatLibrary.generated.h"

class APlayerController;
class UObject;

/**
 * **RTS 模式的作弊函数库**（模式入口）。
 *
 * - 目前只是"用 RTS 的动作基类去调通用库"；以后 RTS 专用的复合操作（一键成军、清场…）也放这里；
 * - 别的模式各写各的库，互不影响（`UAsceCheatLibrary` 只提供通用解析/执行/发现）。
 *
 * 见 `Cheat/作弊系统设计.md`。
 */
UCLASS()
class ASCEGAME_API UAsceRTSCheatLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 解析一整行并执行 RTS 动作 */
	UFUNCTION(BlueprintCallable, Category = "Asce|Cheat|RTS", meta = (WorldContext = "WorldContextObject"))
	static bool RunFromLine(UObject* WorldContextObject, APlayerController* Requester,
		const FString& Line, FString& OutSummary, FString& OutError);
};
