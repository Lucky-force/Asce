// AsceTickContext.h
#pragma once

#include "CoreMinimal.h"

class UWorld;
class UMassEntitySubsystem;
struct FMassEntityManager;
class UAsceSpatialGridSubsystem;
class UAsceBuildingSubsystem;
class UAsceEtherNetworkSubsystem;
class UAscePathFindingSubsystem;
class UAsceMassMovementSimulationSubsystem;
class UAsceFogSubsystem;
class UAsceFogRenderSubsystem;

/**
 * 一帧的共享上下文：由 `UAsceSimulationSubsystem` 在 Tick 里准备好，**按引用**传给各阶段。
 *
 * 为什么不用文件级全局变量：PIE 里同一个进程可能同时跑 server + 多个 client 世界，
 * 全局变量会被互相覆盖；上下文挂在调度器（每个世界一个）上、按引用传递就没有这个问题。
 *
 * 阶段系统需要"别的系统/共享数据"时，都从这里取（而不是自己存别的子系统的指针）。
 */
struct FAsceTickContext
{
	// ── 每帧刷新 ──

	UWorld* World = nullptr;
	float DeltaTime = 0.f;
	/** 引擎帧号（`GFrameCounter`）：阶段里要做"每帧一次"判断时用。 */
	uint64 Frame = 0;
	/** 权威端（非 NM_Client）：阶段里要区分服务器/客户端时用（例如建筑引导只在服务器建实体）。 */
	bool bAuthority = false;

	/** MassEntity 子系统 / 实体管理器（可能为空；`EntityManager` 仅在 `Mass` 有效时有效）。 */
	UMassEntitySubsystem* Mass = nullptr;
	FMassEntityManager* EntityManager = nullptr;

	// ── 阶段系统（`OnWorldBeginPlay` 解析一次；缺的保持 null ⇒ 调度器跳过该阶段）──

	UAsceBuildingSubsystem* Buildings = nullptr;
	UAscePathFindingSubsystem* PathFinding = nullptr;
	UAsceSpatialGridSubsystem* Grid = nullptr;
	UAsceMassMovementSimulationSubsystem* Movement = nullptr;
	UAsceFogSubsystem* Fog = nullptr;
	UAsceEtherNetworkSubsystem* Ether = nullptr;
	UAsceFogRenderSubsystem* FogRender = nullptr;
};
