// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceUnitReplicator.h"

TArray<TUniquePtr<FAsceReplicationCapabilityBase>>& UAsceUnitReplicator::GetCapabilities()
{
	static TArray<TUniquePtr<FAsceReplicationCapabilityBase>> Capabilities;
	return Capabilities;
}

void UAsceUnitReplicator::RegisterCapability(TUniquePtr<FAsceReplicationCapabilityBase> Capability)
{
	GetCapabilities().Add(MoveTemp(Capability));
}

void UAsceUnitReplicator::AddRequirements(FMassEntityQuery& EntityQuery)
{
	for (const TUniquePtr<FAsceReplicationCapabilityBase>& Capability : GetCapabilities())
	{
		Capability->AddRequirements(EntityQuery);
	}
}

void UAsceUnitReplicator::ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext)
{
	for (const TUniquePtr<FAsceReplicationCapabilityBase>& Capability : GetCapabilities())
	{
		Capability->ProcessClientReplication(Context, ReplicationContext);
	}
}
