// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MassCommonTypes.h"
#include "MassEntityHandle.h"
#include "MassExternalSubsystemTraits.h"
#include "AsceNetIDRegistrySubsystem.generated.h"

/**
 * 服务器权威的 NetID -> EntityHandle 反查表。
 *
 * 背景（详见架构文档4.1节）：
 * 客户端和服务器各是独立的FMassEntityManager实例，EntityHandle是各自私有的编号空间，
 * 靠FMassNetworkID关联。MassReplication插件的UMassReplicationSubsystem::EntityInfoMap
 * 只在客户端编译存在（#if UE_REPLICATION_COMPILE_CLIENT_CODE），服务器侧没有提供反查表，
 * 所以RPC从客户端带着NetID上来后，服务器必须自己维护一份注册表才能查到对应的EntityHandle。
 *
 * 生命周期：靠UAsceNetIDRegistryAddProcessor / UAsceNetIDRegistryRemoveProcessor
 * 这两个Observer Processor在FMassNetworkIDFragment被Add/Remove时自动维护，
 * 不需要手动调用Register/Unregister。
 */
UCLASS()
class UAsceNetIDRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UAsceNetIDRegistrySubsystem* Get(const UObject* WorldContextObject);

	// 只允许Observer Processor调用，其他地方不应该手动改这张表
	void RegisterEntity(const FMassNetworkID& NetID, const FMassEntityHandle& EntityHandle);
	void UnregisterEntity(const FMassNetworkID& NetID);

	// 查询接口：服务器收到客户端RPC带着NetID时，用这个查回EntityHandle
	FMassEntityHandle FindEntity(const FMassNetworkID& NetID) const;

	bool IsValidNetID(const FMassNetworkID& NetID) const;

protected:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

private:
	TMap<FMassNetworkID, FMassEntityHandle> NetIDToEntityHandle;
};

template<>
struct TMassExternalSubsystemTraits<UAsceNetIDRegistrySubsystem>
{
	enum
	{
		GameThreadOnly = false,
	};
};
