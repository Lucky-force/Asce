// AsceEtherRegistrationObserverProcessor.cpp
#include "AsceEtherRegistrationObserverProcessor.h"

#include "MassExecutionContext.h"
#include "AsceMass/Ether/AsceEtherNetworkSubsystem.h"
#include "AsceMass/Ether/AsceEtherFragments.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceEtherRegistrationObserverProcessor)

// ── Add ──

UAsceEtherRegistrationAddObserverProcessor::UAsceEtherRegistrationAddObserverProcessor()
	: EntityQuery(*this)
{
	ObservedType = FEtherBuildingFragment::StaticStruct();
	ObservedOperations = EMassObservedOperationFlags::Add;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
}

void UAsceEtherRegistrationAddObserverProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddRequirement<FEtherBuildingFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAsceUnitDefFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UAsceEtherNetworkSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceEtherRegistrationAddObserverProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
	{
		UAsceEtherNetworkSubsystem& Net = Ctx.GetMutableSubsystemChecked<UAsceEtherNetworkSubsystem>();
		const TConstArrayView<FAsceUnitDefFragment> Defs = Ctx.GetFragmentView<FAsceUnitDefFragment>();
		const TConstArrayView<FMassEntityHandle> Entities = Ctx.GetEntities();
		for (int32 i = 0; i < Entities.Num(); ++i)
		{
			// 身份 = 实体定义表行名（数字 ID 的字符串形式）
			const FName BuildingId(*FString::FromInt(Defs[i].UniqueTypeId));
			// 结构性修改走命令队列（tick 开头统一应用）
			Net.PushAddNode(Entities[i], BuildingId);
		}
	});
}

// ── Remove ──

UAsceEtherRegistrationRemoveObserverProcessor::UAsceEtherRegistrationRemoveObserverProcessor()
	: EntityQuery(*this)
{
	ObservedType = FEtherBuildingFragment::StaticStruct();
	ObservedOperations = EMassObservedOperationFlags::Remove;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
}

void UAsceEtherRegistrationRemoveObserverProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddRequirement<FEtherBuildingFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UAsceEtherNetworkSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceEtherRegistrationRemoveObserverProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
	{
		UAsceEtherNetworkSubsystem& Net = Ctx.GetMutableSubsystemChecked<UAsceEtherNetworkSubsystem>();
		for (const FMassEntityHandle E : Ctx.GetEntities())
		{
			Net.UnregisterBuildingByEntity(E);
		}
	});
}
