// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AsceMass/Networking/Core/AsceReplicationCapability.h"

/**
 * "速度同步"能力：任何entity只要挂了FMassDesiredMovementFragment就会被处理，
 * 不依赖任何专属Trait开关。跟坐标同步（AsceTransformReplicationCapability）是姊妹能力，
 * 但独立成一个Capability/FastArrayItem，两者各自独立判脏、互不拖累，详见
 * AsceReplicationPositionHandlers.h/AsceReplicationVelocityHandlers.h类头注释。
 *
 * payload周期性同步（变化超过容差就重新广播），不是只在Add时同步一次。
 *
 * 注意：这一轮LOD可见性判定还是用引擎默认的"距离"（FMassReplicationParameters.LODDistance），
 * 战争迷雾Sector可见性替换LOD计算是下一轮的工作，不在这里。
 */
class FAsceVelocityReplicationCapability : public FAsceReplicationCapabilityBase
{
public:
	virtual void AddRequirements(FMassEntityQuery& EntityQuery) override;
	virtual void ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext) override;
};
