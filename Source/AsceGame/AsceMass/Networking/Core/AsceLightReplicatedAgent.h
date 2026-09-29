// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassReplicationTypes.h"

#include "AsceLightReplicatedAgent.generated.h"

/**
 * **被动（sub）bubble 专用**的轻量 Agent 基类（替代引擎的 `FReplicatedAgentBase`）。见 `Networking/Mass复制设计.md`。
 *
 * 动机（每项实际上线字节）：
 *  - 引擎 `FReplicatedAgentBase` 把 `NetID` + `TemplateID` 都当复制属性；而 FastArray 每次标脏是**整项全量**序列化
 *    （`FastArraySerializer.h` 的 `FastArrayDeltaSerialize`：写元素 ID + `NetSerializeStruct(整 item)`），
 *    于是 `TemplateID`（`FGuid`16B + `uint32`4B + `uint64`8B ≈ **28B**）**每次变化都被重发**，占单项开销一大半；
 *  - 客户端**只有 owner bubble（Transform）**需要 `TemplateID`：它按 template 批量 spawn 实体
 *    （`MassClientBubbleHandler.h` 的 `PostReplicatedAddEntitiesHelper`）。被动 bubble 的客户端处理
 *    （`TAscePassiveClientBubbleHandler`）只用 `NetID` 认领**已生成**的实体 ⇒ 它的 `TemplateID` 上线后没有读者。
 *
 * 做法：`TemplateID` 标 **`NotReplicated`**（引擎序列化会跳过 `CPF_RepSkip` 属性，见 `FRepLayout::InitFromStruct`）——
 * **服务器内存里照常 Set**（满足引擎服务器侧 debug 校验 `DebugValidateBubbleOnServer` 的
 * `checkf(GetTemplateID().IsValid())` 与 `FReplicationTemplateIDFragment` 比对），只是**不上线**。每项每次省 ~28B。
 *
 * **不变量**：凡是走被动通道复制的实体，**必须同时被 owner（Transform）通道复制**（否则客户端没人给它 spawn）。
 * 项目里所有复制实体都带 `FTransformFragment`，满足。
 *
 * **注意**：引擎客户端的 debug 校验（`DebugValidateBubbleOnClient` 的 `check(GetTemplateID().IsValid())`）
 * 被 `UE_ALLOW_DEBUG_REPLICATION` 包住，默认 **0**（见 `MassReplicationTypes.h`）。若手动 `UE_DEBUG_REPLICATION=1`，
 * 该 check 会在客户端失败——那是调试开关，正常构建不受影响。
 *
 * 提供引擎模板 duck-typing 要求的 `Get/SetNetID`、`Get/SetTemplateID`。owner（Transform）通道继续用引擎的
 * `FReplicatedAgentBase`（它必须复制 `TemplateID`）。
 */
USTRUCT()
struct FAsceLightReplicatedAgentBase
{
	GENERATED_BODY()

public:
	void SetNetID(const FMassNetworkID InNetID) { NetID = InNetID; }
	FMassNetworkID GetNetID() const { return NetID; }

	void SetTemplateID(const FMassEntityTemplateID InTemplateID) { TemplateID = InTemplateID; }
	FMassEntityTemplateID GetTemplateID() const { return TemplateID; }

	/** 公平分帧调度用：上次真正发出去时的服务号（0 = 从未服务）。见 `AsceServeWindow.h` */
	uint32 GetLastServedTicket() const { return LastServedTicket; }
	uint32& GetLastServedTicketMutable() { return LastServedTicket; }

	/** 按距离的更新间隔用：上次真正发出去时的世界时间（秒）。见 `AsceReplicationInterval.h` */
	float GetLastUpdateTime() const { return LastUpdateTime; }
	void SetLastUpdateTime(const float InTime) { LastUpdateTime = InTime; }

private:
	/** 客户端认领实体的唯一键（也是共享表 `EntityInfoMap` 的键）——必须复制 */
	UPROPERTY()
	FMassNetworkID NetID;

	/** 只在**服务器内存**里用于引擎校验；客户端 spawn 由 owner（Transform）通道负责 ⇒ 不复制 */
	UPROPERTY(NotReplicated)
	FMassEntityTemplateID TemplateID;

	/** 上次真正发出去时的服务号（0 = 从未服务）；纯服务器本地调度用，不复制 */
	UPROPERTY(NotReplicated)
	uint32 LastServedTicket = 0;

	/** 上次真正发出去时的世界时间（秒；0 = 从未服务）；纯服务器本地调度用，不复制 */
	UPROPERTY(NotReplicated)
	float LastUpdateTime = 0.f;
};
