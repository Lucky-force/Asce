// AsceBuildingFootprintObserverProcessor.cpp
#include "AsceBuildingFootprintObserverProcessor.h"

#include "AsceBuildingSubsystem.h"
#include "AsceMass/Fragments/AsceFootprintFragment.h"
#include "MassExecutionContext.h"   // FMassExecutionContext（单独编译时需要；unity 下被别人捎带才没暴露）

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
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
	{
		// 子系统必须在 query 作用域内取：`ForEachEntityChunk` 会把 query 的子系统依赖缓存进 context，
		// 在 lambda 外取时 bitset 还是空的 ⇒ 命中 Mass 的 "Missing TMassExternalSubsystemTraits" ensure
		UAsceBuildingSubsystem& Buildings = Ctx.GetMutableSubsystemChecked<UAsceBuildingSubsystem>();
		const TConstArrayView<FMassEntityHandle> Entities = Ctx.GetEntities();
		for (const FMassEntityHandle Entity : Entities)
		{
			// 建筑完工 ⇒ 释放建造占位（costfield 已由同一阶段的 costfield 观察者写好）
			Buildings.ConsumePendingBuild(Entity);
		}
	});
}
