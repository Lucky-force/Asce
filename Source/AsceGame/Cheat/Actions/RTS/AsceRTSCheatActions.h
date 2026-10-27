// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Cheat/Actions/RTS/AsceRTSCheatAction.h"

#include "AsceRTSCheatActions.generated.h"

/**
 * RTS 的第一批作弊动作（每个都是薄壳：只声明参数 + 转给 `AsceRTSCheatOps`）。
 *
 * | 命令 | 作用 |
 * |---|---|
 * | `SpawnUnits DefRow=101 Count=5 Pos=x,y,z Team=-1 Spread=200` | 生成单位（走生成队列） |
 * | `PlaceBuilding DefRow=201 CellX=10 CellY=20 Rotation=0 Team=-1` | 放置建筑（先校验，**不强制**） |
 * | `DestroyEntity Entity=123` | 删除实体（建筑走拆除，其它走销毁队列） |
 * | `ListDefs UnitsOnly=0 BuildingsOnly=0` | 列出可生成的单位/建筑 |
 */

/** 生成单位（`Pos` 为空 = 鼠标下的地面点；`Team=-1` = 请求者阵营，`0` = 中立） */
UCLASS()
class ASCEGAME_API UAsceRTSCheatActionSpawnUnits : public UAsceRTSCheatAction
{
	GENERATED_BODY()

public:
	virtual FName GetActionName() const override { return FName(TEXT("SpawnUnits")); }
	virtual FText GetDisplayName() const override;
	virtual FText GetCategory() const override;
	virtual TMap<FName, FAsceCheatActionArg> BuildArgs() const override;
	virtual bool Execute(const FAsceCheatContext& Context, const TMap<FName, FAsceCheatActionArg>& Args
		, FString& OutSummary, FString& OutError) const override;
};

/** 放置建筑（格子坐标为空 = 用鼠标位置换算；永远先 `ValidatePlacement`） */
UCLASS()
class ASCEGAME_API UAsceRTSCheatActionPlaceBuilding : public UAsceRTSCheatAction
{
	GENERATED_BODY()

public:
	virtual FName GetActionName() const override { return FName(TEXT("PlaceBuilding")); }
	virtual FText GetDisplayName() const override;
	virtual FText GetCategory() const override;
	virtual TMap<FName, FAsceCheatActionArg> BuildArgs() const override;
	virtual bool Execute(const FAsceCheatContext& Context, const TMap<FName, FAsceCheatActionArg>& Args
		, FString& OutSummary, FString& OutError) const override;
};

/** 删除一个实体（建筑 → 拆除；其它 → 销毁队列） */
UCLASS()
class ASCEGAME_API UAsceRTSCheatActionDestroyEntity : public UAsceRTSCheatAction
{
	GENERATED_BODY()

public:
	virtual FName GetActionName() const override { return FName(TEXT("DestroyEntity")); }
	virtual FText GetDisplayName() const override;
	virtual FText GetCategory() const override;
	virtual TMap<FName, FAsceCheatActionArg> BuildArgs() const override;
	virtual bool Execute(const FAsceCheatContext& Context, const TMap<FName, FAsceCheatActionArg>& Args
		, FString& OutSummary, FString& OutError) const override;
};

/** 列出可生成的实体定义（排查/手输 def 用） */
UCLASS()
class ASCEGAME_API UAsceRTSCheatActionListDefs : public UAsceRTSCheatAction
{
	GENERATED_BODY()

public:
	virtual FName GetActionName() const override { return FName(TEXT("ListDefs")); }
	virtual FText GetDisplayName() const override;
	virtual FText GetCategory() const override;
	virtual TMap<FName, FAsceCheatActionArg> BuildArgs() const override;
	virtual bool Execute(const FAsceCheatContext& Context, const TMap<FName, FAsceCheatActionArg>& Args
		, FString& OutSummary, FString& OutError) const override;
};
