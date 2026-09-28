// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AsceMass/Networking/Core/AsceReplicationCapability.h"

/**
 * "动作态同步"能力：挂 `FAsceActionFragment` 的entity（当前 = 所有单位）自动同步
 * 动作状态/动作id/序号/播放速度/朝向 —— 驱动客户端转身与后续VAT动画。
 *
 * 服务器侧只走**边沿**：`SetActionStateFromFragment` 内部逐字段比较，动作切换时才标脏
 * （见 AsceReplicationActionHandlers.h）。
 */
class FAsceActionReplicationCapability : public FAsceReplicationCapabilityBase
{
public:
	virtual void AddRequirements(FMassEntityQuery& EntityQuery) override;
	virtual void ProcessChunk(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext
		, const FMassClientHandle& ClientHandle, TArrayView<FMassReplicatedAgentHandle> InOutMarkers) override;
};
