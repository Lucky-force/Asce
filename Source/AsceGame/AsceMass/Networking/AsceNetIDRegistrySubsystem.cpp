// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceNetIDRegistrySubsystem.h"

#include "Engine/World.h"

UAsceNetIDRegistrySubsystem* UAsceNetIDRegistrySubsystem::Get(const UObject* WorldContextObject)
{
	if (!IsValid(WorldContextObject))
	{
		return nullptr;
	}
	const UWorld* World = WorldContextObject->GetWorld();
	return IsValid(World) ? World->GetSubsystem<UAsceNetIDRegistrySubsystem>() : nullptr;
}

bool UAsceNetIDRegistrySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// 服务器/单机(Standalone)才需要这张反查表；纯客户端有引擎自带的EntityInfoMap，不需要这个。
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World == nullptr || World->GetNetMode() != NM_Client;
}

void UAsceNetIDRegistrySubsystem::RegisterEntity(const FMassNetworkID& NetID, const FMassEntityHandle& EntityHandle)
{
	if (!NetID.IsValid())
	{
		return;
	}
	NetIDToEntityHandle.Add(NetID, EntityHandle);
}

void UAsceNetIDRegistrySubsystem::UnregisterEntity(const FMassNetworkID& NetID)
{
	if (!NetID.IsValid())
	{
		return;
	}
	NetIDToEntityHandle.Remove(NetID);
}

FMassEntityHandle UAsceNetIDRegistrySubsystem::FindEntity(const FMassNetworkID& NetID) const
{
	if (const FMassEntityHandle* Found = NetIDToEntityHandle.Find(NetID))
	{
		return *Found;
	}
	UE_LOG(LogTemp, Warning, TEXT("[NetIDRegistry] 未命中 NetID=%s（表内 %d 条）"), *NetID.Describe(), NetIDToEntityHandle.Num());
	return FMassEntityHandle();
}

bool UAsceNetIDRegistrySubsystem::IsValidNetID(const FMassNetworkID& NetID) const
{
	return NetID.IsValid() && NetIDToEntityHandle.Contains(NetID);
}
