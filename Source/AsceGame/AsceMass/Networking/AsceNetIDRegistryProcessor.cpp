// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceNetIDRegistryProcessor.h"

#include "MassExecutionContext.h"
#include "MassReplicationFragments.h"
#include "AsceNetIDRegistrySubsystem.h"

// ────────────────────────────────────────────
// Add
// ────────────────────────────────────────────

UAsceNetIDRegistryAddProcessor::UAsceNetIDRegistryAddProcessor()
	: EntityQuery(*this)
{
	ObservedType = FMassNetworkIDFragment::StaticStruct();
	ObservedOperations = EMassObservedOperationFlags::Add;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);

	// 必须在引擎给NetID赋真实值之后执行，否则读到的是默认无效NetID
	ExecutionOrder.ExecuteAfter.Add(UMassNetworkIDFragmentInitializer::StaticClass()->GetFName());
}

void UAsceNetIDRegistryAddProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassNetworkIDFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UAsceNetIDRegistrySubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceNetIDRegistryAddProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
		UAsceNetIDRegistrySubsystem& Registry = Context.GetMutableSubsystemChecked<UAsceNetIDRegistrySubsystem>();
		const TConstArrayView<FMassNetworkIDFragment> NetIDs = Context.GetFragmentView<FMassNetworkIDFragment>();
		const TConstArrayView<FMassEntityHandle> Handles = Context.GetEntities();

		const int32 NumEntities = Context.GetNumEntities();
		for (int32 i = 0; i < NumEntities; ++i)
		{
			Registry.RegisterEntity(NetIDs[i].NetID, Handles[i]);
		}
	});
}

// ────────────────────────────────────────────
// Remove
// ────────────────────────────────────────────

UAsceNetIDRegistryRemoveProcessor::UAsceNetIDRegistryRemoveProcessor()
	: EntityQuery(*this)
{
	ObservedType = FMassNetworkIDFragment::StaticStruct();
	ObservedOperations = EMassObservedOperationFlags::Remove;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
}

void UAsceNetIDRegistryRemoveProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassNetworkIDFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UAsceNetIDRegistrySubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceNetIDRegistryRemoveProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
		UAsceNetIDRegistrySubsystem& Registry = Context.GetMutableSubsystemChecked<UAsceNetIDRegistrySubsystem>();
		const TConstArrayView<FMassNetworkIDFragment> NetIDs = Context.GetFragmentView<FMassNetworkIDFragment>();

		const int32 NumEntities = Context.GetNumEntities();
		for (int32 i = 0; i < NumEntities; ++i)
		{
			Registry.UnregisterEntity(NetIDs[i].NetID);
		}
	});
}
