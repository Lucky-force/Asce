// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceReplicationBootstrapSubsystem.h"

#include "AsceMass/Networking/Capabilities/Transform/AsceTransformReplication.h"
#include "AsceMass/Networking/Capabilities/Transform/AsceTransformReplicationCapability.h"
#include "AsceMass/Networking/Capabilities/Velocity/AsceVelocityReplication.h"
#include "AsceMass/Networking/Capabilities/Velocity/AsceVelocityReplicationCapability.h"
#include "AsceMass/Networking/Capabilities/Health/AsceHealthReplication.h"
#include "AsceMass/Networking/Capabilities/Health/AsceHealthReplicationCapability.h"
#include "AsceMass/Networking/Core/AsceUnitReplicator.h"
#include "MassReplicationSubsystem.h"

namespace
{
	/**
	 * Capability只需要注册一次，多个World（PIE多开、Client+Server Listen Server等）会重复调用
	 * PostInitialize，这里用static bool去重，避免同一个能力被塞进数组好几遍。
	 */
	void RegisterCapabilitiesOnce()
	{
		static bool bRegistered = false;
		if (bRegistered)
		{
			return;
		}
		bRegistered = true;

		// 每新增一个"同步能力"（坐标/速度/生命值/护盾……），都要在这里补一行注册，
		// entity那边的UMassReplicationTrait::Params.ReplicatorClass统一填UAsceUnitReplicator，
		// 不用为每个能力单独配一个Replicator。
		//
		// 坐标和速度虽然都来自移动，但是两个独立的Capability——各自独立判脏、独立广播，
		// 不会因为合并payload而互相拖累，详见AsceReplicationPositionHandlers.h类头注释。
		UAsceUnitReplicator::RegisterCapability(MakeUnique<FAsceTransformReplicationCapability>());
		UAsceUnitReplicator::RegisterCapability(MakeUnique<FAsceVelocityReplicationCapability>());
		UAsceUnitReplicator::RegisterCapability(MakeUnique<FAsceHealthReplicationCapability>());
	}
}

void UAsceReplicationBootstrapSubsystem::PostInitialize()
{
	Super::PostInitialize();

	RegisterCapabilitiesOnce();

	if (UMassReplicationSubsystem* ReplicationSubsystem = UWorld::GetSubsystem<UMassReplicationSubsystem>(GetWorld()))
	{
		// BubbleInfo（数据容器）依然是每个能力独立注册一份，可以自由叠加，
		// 跟上面"Replicator只能有一个"是两回事，见架构文档说明。
		ReplicationSubsystem->RegisterBubbleInfoClass(AAsceTransformClientBubbleInfo::StaticClass());
		ReplicationSubsystem->RegisterBubbleInfoClass(AAsceVelocityClientBubbleInfo::StaticClass());
		ReplicationSubsystem->RegisterBubbleInfoClass(AAsceHealthClientBubbleInfo::StaticClass());
	}
}
