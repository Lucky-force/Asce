// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AsceMass/Networking/Core/AsceReplicationCapability.h"

/**
 * "坐标同步"能力：任何entity只要挂了FTransformFragment就会被处理，不依赖任何专属Trait开关。
 * 仿照MassCrowd的UMassCrowdReplicator写的，只是从独立Replicator降级成了插件式Capability。
 *
 * payload只有位置，周期性同步（变化超过容差就重新广播），不是只在Add时同步一次；
 * 速度已经拆到独立的FAsceVelocityReplicationCapability（见Capabilities/Velocity/），
 * 两者各自独立判脏，互不拖累，详见AsceTransformReplication.h/AsceReplicationPositionHandlers.h。
 *
 * 注意：这一轮LOD可见性判定还是用引擎默认的"距离"（FMassReplicationParameters.LODDistance），
 * 战争迷雾Sector可见性替换LOD计算是下一轮的工作，不在这里。
 */
class FAsceTransformReplicationCapability : public FAsceReplicationCapabilityBase
{
public:
	virtual void AddRequirements(FMassEntityQuery& EntityQuery) override;
	virtual void ProcessChunk(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext
		, const FMassClientHandle& ClientHandle, TArrayView<FMassReplicatedAgentHandle> InOutMarkers) override;
};
