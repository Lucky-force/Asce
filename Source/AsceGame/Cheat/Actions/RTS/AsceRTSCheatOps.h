// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityHandle.h"

#include "AsceMass/Data/AsceEntityDefTypes.h"

class APlayerController;
class UWorld;

/**
 * **RTS 作弊操作的具体实现**（动作只做"参数 → 调这里"）。
 *
 * 这里也是以后 RTS 复合作弊（一键成军 / 清场 / 全图侦察…）的落点；所有函数：
 * - **只允许权威端**；
 * - 写 `OutSummary`（成功描述）/`OutError`（失败原因），不打印；
 * - 生成走 **生成队列**（阶段 8 才真正创建实体），建筑走 **`ValidatePlacement` 后再 `PlaceBuilding`**（**永不强制放置**）。
 */
namespace AsceRTSCheatOps
{
	/** 校验 def 行存在、且是"单位"（`bWantBuilding=false`）或"建筑"（`true`） */
	bool ValidateDefRow(FName DefRowName, bool bWantBuilding, FAsceEntityDefRow& OutRow, FString& OutError);

	/** 在 `Center` 周围环形散开生成 `Count` 个单位（`TeamId`：-1 = 中立? **不能传 -1**，调用方先解析） */
	bool SpawnUnits(UWorld* World, FName DefRowName, const FVector& Center, int32 Count, float SpreadRadius, int32 TeamId
		, FString& OutSummary, FString& OutError);

	/** 按逻辑网格中心格放置一个建筑（先校验；违规直接失败） */
	bool PlaceBuilding(UWorld* World, FName DefRowName, const FIntPoint& CenterCell, int32 Rotation, int32 TeamId
		, FString& OutSummary, FString& OutError);

	/** 删除实体：建筑（已建成 / 施工中）走 `DemolishBuilding`，其它走 `RequestDestroy` */
	bool DestroyEntity(UWorld* World, const FMassEntityHandle& Entity, FString& OutSummary, FString& OutError);

	/** 列出可生成的 def（单位 / 建筑），写进 `OutSummary` */
	bool ListDefs(UWorld* World, bool bUnitsOnly, bool bBuildingsOnly, FString& OutSummary, FString& OutError);

	/** 鼠标下的地面点（失败写 `OutError`）；用于"不给位置就用光标" */
	bool GetCursorWorldLocation(APlayerController* PC, FVector& OutLocation, FString& OutError);

	/** 世界坐标 → 建筑逻辑网格（1 格 = 100cm） */
	FIntPoint WorldToLogicalCell(const FVector& WorldLocation);

	/** 把 `Team` 参数解析成最终 TeamId：`-1` = 请求者阵营，`0` = 中立，`>0` = 指定 */
	int32 ResolveTeamId(int32 TeamParam, int32 RequesterTeamId);
}
