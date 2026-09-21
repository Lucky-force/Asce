// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceGridBuildingsUpdateProcessor.h"

#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"   // FTransformFragment（读基座 Z）
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Fragments/AsceFootprintFragment.h"
#include "AsceSpatialGridSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceGridBuildingsUpdateProcessor)

UAsceGridBuildingsUpdateProcessor::UAsceGridBuildingsUpdateProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = false;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
}

void UAsceGridBuildingsUpdateProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddTagRequirement<FAsceBuildingTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FAsceGridFootprintFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);   // 基座 Z（贴地后的实体 Z）
	EntityQuery.AddSubsystemRequirement<UAsceSpatialGridSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceGridBuildingsUpdateProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 与单位层同一套并行范式：并行 chunk + 每粗格自旋锁（`FAsceGridBuildingsData::AddBuilding`）
	EntityQuery.ParallelForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FAsceGridFootprintFragment> Footprints = Ctx.GetFragmentView<FAsceGridFootprintFragment>();
		const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassEntityHandle> Handles = Ctx.GetEntities();
		const int32 Num = Ctx.GetNumEntities();
		UAsceSpatialGridSubsystem& Grid = Ctx.GetMutableSubsystemChecked<UAsceSpatialGridSubsystem>();
		for (int32 i = 0; i < Num; ++i)
		{
			const FAsceGridFootprintFragment& Foot = Footprints[i];
			FAsceGridBuildingPart Part;
			Part.Entity = Handles[i];
			// footprint 已经是"旋转后的最终 AABB"（与 costfield/视野同一口径），直接登记
			Part.Origin = Foot.OriginGridLocation;
			Part.Size = FIntPoint(FMath::Max(1, Foot.SizeX), FMath::Max(1, Foot.SizeY));
			// 命中盒高度：用视野遮挡高度；0 会给一个最小值，避免选择盒退化成平面
			Part.HeightCm = FMath::Max(100, static_cast<int32>(Foot.OcclusionHeightCm));
			// 基座 Z：建筑贴地后的实体 Z（选择盒从这个高度起算）
			Part.BaseZ = static_cast<float>(Transforms[i].GetTransform().GetLocation().Z);
			Grid.AddBuildingPart(Part);
		}
	});
}
