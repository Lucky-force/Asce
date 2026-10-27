// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Cheat/AsceCheatTypes.h"

#include "AsceRTSCheatArgTypes.generated.h"

/**
 * RTS 模式**专用**的参数类型（放在模式目录，基础层不认识实体/动作/阵营）。
 *
 * 加一个 RTS 专用类型 = 在这里加一个 USTRUCT，UI 下拉自动就有候选。
 */
USTRUCT()
struct ASCEGAME_API FAsceCheatArgDefRow : public FAsceCheatArgName
{
	GENERATED_BODY()

	/** 只允许单位行 / 只允许建筑行（都不设 = 只列可生成的单位与建筑） */
	UPROPERTY(EditAnywhere, Category = "Cheat")
	bool bUnitsOnly = false;

	UPROPERTY(EditAnywhere, Category = "Cheat")
	bool bBuildingsOnly = false;

	virtual FString GetTypeName() const override { return TEXT("DefRow"); }
	virtual TArray<FString> GetSuggestions(const UWorld* World) const override;
};

/** 动作行（同上的候选，来自 `FAsceActionDef`） */
USTRUCT()
struct ASCEGAME_API FAsceCheatArgActionRow : public FAsceCheatArgName
{
	GENERATED_BODY()

	virtual FString GetTypeName() const override { return TEXT("ActionRow"); }
	virtual TArray<FString> GetSuggestions(const UWorld* World) const override;
};

/** 阵营：`-1` = 用请求者默认，`0` = 中立；`>0` = TeamId */
USTRUCT()
struct ASCEGAME_API FAsceCheatArgTeam : public FAsceCheatArgInt
{
	GENERATED_BODY()

	virtual FString GetTypeName() const override { return TEXT("Team"); }
	virtual TArray<FString> GetSuggestions(const UWorld* World) const override;
};
