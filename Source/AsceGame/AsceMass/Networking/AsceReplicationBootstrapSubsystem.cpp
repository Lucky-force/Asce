// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceReplicationBootstrapSubsystem.h"

#include "AsceMass/Networking/Capabilities/Transform/AsceTransformReplication.h"
#include "AsceMass/Networking/Capabilities/Transform/AsceTransformReplicationCapability.h"
#include "AsceMass/Networking/Capabilities/Velocity/AsceVelocityReplication.h"
#include "AsceMass/Networking/Capabilities/Velocity/AsceVelocityReplicationCapability.h"
#include "AsceMass/Networking/Capabilities/Health/AsceHealthReplication.h"
#include "AsceMass/Networking/Capabilities/Health/AsceHealthReplicationCapability.h"
#include "AsceMass/Networking/Capabilities/Mana/AsceManaReplication.h"
#include "AsceMass/Networking/Capabilities/Mana/AsceManaReplicationCapability.h"
#include "AsceMass/Networking/Capabilities/Team/AsceTeamReplication.h"
#include "AsceMass/Networking/Capabilities/Team/AsceTeamReplicationCapability.h"
#include "AsceMass/Networking/Capabilities/Action/AsceActionReplication.h"
#include "AsceMass/Networking/Capabilities/Action/AsceActionReplicationCapability.h"
#include "AsceMass/Networking/Core/AsceUnitReplicator.h"
#include "AsceMass/Data/AsceEntityDefLibrary.h"
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
		// 复制组参数（ReplicatorClass=UAsceUnitReplicator + 代表 BubbleInfoClass）由
		// `UAsceRTSEntityTrait` 自动写入模板，模板不用再手动加引擎 "Replication" trait。
		//
		// 坐标和速度虽然都来自移动，但是两个独立的Capability——各自独立判脏、独立广播，
		// 不会因为合并payload而互相拖累，详见AsceReplicationPositionHandlers.h类头注释。
		UAsceUnitReplicator::RegisterCapability(MakeUnique<FAsceTransformReplicationCapability>());
		UAsceUnitReplicator::RegisterCapability(MakeUnique<FAsceVelocityReplicationCapability>());
		UAsceUnitReplicator::RegisterCapability(MakeUnique<FAsceHealthReplicationCapability>());
		UAsceUnitReplicator::RegisterCapability(MakeUnique<FAsceManaReplicationCapability>());
		UAsceUnitReplicator::RegisterCapability(MakeUnique<FAsceTeamReplicationCapability>());
		UAsceUnitReplicator::RegisterCapability(MakeUnique<FAsceActionReplicationCapability>());
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
		ReplicationSubsystem->RegisterBubbleInfoClass(AAsceManaClientBubbleInfo::StaticClass());
		ReplicationSubsystem->RegisterBubbleInfoClass(AAsceTeamClientBubbleInfo::StaticClass());
		ReplicationSubsystem->RegisterBubbleInfoClass(AAsceActionClientBubbleInfo::StaticClass());
	}
}

void UAsceReplicationBootstrapSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 实体模板预注册（**服务器和客户端都要跑**）：Mass 复制在客户端是按 TemplateID
	// 本地 spawn 实体的（bubble PostReplicatedAdd → UMassSpawnerSubsystem::SpawnEntities，
	// MassClientBubbleHandler.h），而客户端不跑 EntityFactory（生成被权威端拦截，
	// AsceEntityFactory.cpp）⇒ 客户端不预注册就会命中
	// checkf("SpawnEntities: TemplateID must have been registered!")。
	// 模板 id = UMassEntityConfigAsset 路径的确定性 GUID（MassEntityConfigAsset.cpp），
	// 两端同一资产必然一致；对同一 World 幂等，单机/服务器重复调也不会重复建。
	const int32 RegisteredCount = UAsceEntityDefLibrary::PreRegisterAllEntityTemplates(InWorld);
	UE_LOG(LogTemp, Log, TEXT("[Replication] 实体模板预注册完成：%d 个"), RegisteredCount);
}
