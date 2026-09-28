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

	// 必须在引擎 UMassNetworkIDFragmentInitializer（给NetID赋真实值）之后执行，否则读到默认无效 NetID、
	// RegisterEntity 会跳过，而且不会再有第二次 Add 事件 ⇒ 注册表永远为空。
	// 注意：**观察者管线的排序只认数值 ExecutionPriority**（FMassRuntimePipeline::SortByExecutionPriority
	// 按降序排），不认 ExecutionOrder.ExecuteAfter！初始化器默认 Priority=0，所以这里用负数排到它后面。
	ExecutionPriority = -1;
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
		int32 InvalidCount = 0;
		for (int32 i = 0; i < NumEntities; ++i)
		{
			if (!NetIDs[i].NetID.IsValid())
			{
				++InvalidCount;
			}
			Registry.RegisterEntity(NetIDs[i].NetID, Handles[i]);
		}
		if (InvalidCount > 0)
		{
			// 正常不应该出现：说明本观察器排到了 UMassNetworkIDFragmentInitializer 之前（见构造函数注释）
			UE_LOG(LogTemp, Warning, TEXT("[NetIDRegistry] Add observer：%d 个实体里有 %d 个 NetID 无效，注册被跳过"), NumEntities, InvalidCount);
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
