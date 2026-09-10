// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceCostFieldFootprintObserverProcessor.h"

#include "MassExecutionContext.h"

#include "AscePathFindingSubsystem.h"
#include "AsceMass/Fragments/AsceFootprintFragment.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceCostFieldFootprintObserverProcessor)

namespace
{
	// CostField 语义（见 AsceFlowFieldCores.h：IsGridBlockStateChanged / 初始化代价场）：
	// 255 = 不可通行(block)，1 = 可通行。建筑占用的格在 Add 时写 255、Remove 时恢复为 1。
	constexpr uint8 kBlockCost = MAX_uint8;
	constexpr uint8 kPassableCost = 1;

	/** 把 footprint 覆盖的格子逐格写 cost；返回是否至少处理了一格。 */
	void ApplyCostToFootprint(FWorldMapData& WorldMapData, const FAsceGridFootprintFragment& Footprint, uint8 NewCost)
	{
		const int32 GridCountX = WorldMapData.GetGridCountX();
		const int32 GridCountY = WorldMapData.GetGridCountY();
		const int32 X0 = FMath::Max(0, Footprint.OriginGridLocation.X);
		const int32 Y0 = FMath::Max(0, Footprint.OriginGridLocation.Y);
		const int32 X1 = FMath::Min(GridCountX - 1, Footprint.OriginGridLocation.X + Footprint.SizeX - 1);
		const int32 Y1 = FMath::Min(GridCountY - 1, Footprint.OriginGridLocation.Y + Footprint.SizeY - 1);
		if (X0 > X1 || Y0 > Y1 || GridCountX <= 0)
		{
			return;
		}

		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				WorldMapData.SetCostField(static_cast<uint32>(X + Y * GridCountX), NewCost);
			}
		}
	}
}

// ────────────────────────────────────────────
// Add（建筑入场：写阻挡）
// ────────────────────────────────────────────

UAsceCostFieldFootprintAddObserverProcessor::UAsceCostFieldFootprintAddObserverProcessor()
	: EntityQuery(*this)
{
	ObservedType = FAsceGridFootprintFragment::StaticStruct();
	ObservedOperations = EMassObservedOperationFlags::Add;
	// Server 权威侧才改代价数据（Client 等 FastArray 复制）
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
}

void UAsceCostFieldFootprintAddObserverProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FAsceGridFootprintFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UAscePathFindingSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceCostFieldFootprintAddObserverProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
	{
		UAscePathFindingSubsystem& PathFinding = Ctx.GetMutableSubsystemChecked<UAscePathFindingSubsystem>();
		FWorldMapData* WorldMapData = PathFinding.GetWorldMapDataMutableForAuthority();
		if (!WorldMapData)
		{
			return; // WorldMap 尚未就绪（replicator 未到位 / 非 authority）
		}

		const TConstArrayView<FAsceGridFootprintFragment> Footprints = Ctx.GetFragmentView<FAsceGridFootprintFragment>();
		for (const FAsceGridFootprintFragment& Footprint : Footprints)
		{
			ApplyCostToFootprint(*WorldMapData, Footprint, kBlockCost);
		}
	});
}

// ────────────────────────────────────────────
// Remove（建筑退场：恢复可通行）
// ────────────────────────────────────────────

UAsceCostFieldFootprintRemoveObserverProcessor::UAsceCostFieldFootprintRemoveObserverProcessor()
	: EntityQuery(*this)
{
	ObservedType = FAsceGridFootprintFragment::StaticStruct();
	ObservedOperations = EMassObservedOperationFlags::Remove;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
}

void UAsceCostFieldFootprintRemoveObserverProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FAsceGridFootprintFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UAscePathFindingSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceCostFieldFootprintRemoveObserverProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
	{
		UAscePathFindingSubsystem& PathFinding = Ctx.GetMutableSubsystemChecked<UAscePathFindingSubsystem>();
		FWorldMapData* WorldMapData = PathFinding.GetWorldMapDataMutableForAuthority();
		if (!WorldMapData)
		{
			return;
		}

		const TConstArrayView<FAsceGridFootprintFragment> Footprints = Ctx.GetFragmentView<FAsceGridFootprintFragment>();
		for (const FAsceGridFootprintFragment& Footprint : Footprints)
		{
			ApplyCostToFootprint(*WorldMapData, Footprint, kPassableCost);
		}
	});
}
