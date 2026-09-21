// AsceBuildingFootprintObserverProcessor.cpp
#include "AsceBuildingFootprintObserverProcessor.h"

#include "AsceBuildingSubsystem.h"
#include "AsceMass/Fragments/AsceFootprintFragment.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceBuildingFootprintObserverProcessor)

UAsceBuildingFootprintObserverProcessor::UAsceBuildingFootprintObserverProcessor()
	: EntityQuery(*this)
{
	ObservedType = FAsceGridFootprintFragment::StaticStruct();
	ObservedOperations = EMassObservedOperationFlags::Add;
	// 只有权威端会创建建筑 ⇒ 也只有权威端有"建造中占位"要释放
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
}

void UAsceBuildingFootprintObserverProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FAsceGridFootprintFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UAsceBuildingSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceBuildingFootprintObserverProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UAsceBuildingSubsystem& Buildings = Context.GetMutableSubsystemChecked<UAsceBuildingSubsystem>();
	EntityQuery.ForEachEntityChunk(Context, [&Buildings](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FMassEntityHandle> Entities = Ctx.GetEntities();
		for (const FMassEntityHandle Entity : Entities)
		{
			// 建筑完工 ⇒ 释放建造占位（costfield 已由同一阶段的 costfield 观察者写好）
			Buildings.ConsumePendingBuild(Entity);
		}
	});
}
