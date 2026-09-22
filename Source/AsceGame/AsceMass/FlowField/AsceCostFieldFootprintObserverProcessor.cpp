// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceCostFieldFootprintObserverProcessor.h"

#include "MassExecutionContext.h"

#include "AscePathFindingSubsystem.h"
#include "AsceLayeredCostField.h"
#include "AsceMass/Fragments/AsceFootprintFragment.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceCostFieldFootprintObserverProcessor)

namespace
{
	// 代价分层语义（见 AsceLayeredCostField.h）：
	// - 建筑层（BuildingCost）：0 = 无建筑，AsceCostField::BlockedCost = 建筑占用阻挡；
	// - Add/Remove 只改建筑层，最终 CostFields 由"层组合"统一写回（BaseLand 基线保证 Remove 能还原地形原值）。
	constexpr uint8 kNoBuildingCost = 0;

	/** 把 footprint 覆盖格写进建筑层，并逐格 flush（组合结果与现值不同才写回 CostFields）。 */
	void ApplyBuildingFootprint(FAsceLayeredCostField& LayeredCostField, FWorldMapData& WorldMapData,
		const FAsceGridFootprintFragment& Footprint, uint8 NewBuildingCost)
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
				const uint32 GridIndex = static_cast<uint32>(X + Y * GridCountX);
				LayeredCostField.SetBuildingCost(GridIndex, NewBuildingCost);
				LayeredCostField.FlushCell(GridIndex, WorldMapData);
			}
		}
	}
}

// ────────────────────────────────────────────
// Add（建筑入场：建筑层写阻挡）
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
		FAsceLayeredCostField* LayeredCostField = PathFinding.GetLayeredCostFieldMutableForAuthority();
		if (!WorldMapData || !LayeredCostField)
		{
			return; // WorldMap / 代价分层尚未就绪（replicator 未到位 / 非 authority）
		}

		const TConstArrayView<FAsceGridFootprintFragment> Footprints = Ctx.GetFragmentView<FAsceGridFootprintFragment>();
		for (const FAsceGridFootprintFragment& Footprint : Footprints)
		{
			ApplyBuildingFootprint(*LayeredCostField, *WorldMapData, Footprint, AsceCostField::BlockedCost);
		}
	});
}

// ────────────────────────────────────────────
// Remove（建筑退场：清建筑层，由 BaseLand 基线还原地形）
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
		FAsceLayeredCostField* LayeredCostField = PathFinding.GetLayeredCostFieldMutableForAuthority();
		if (!WorldMapData || !LayeredCostField)
		{
			return;
		}

		const TConstArrayView<FAsceGridFootprintFragment> Footprints = Ctx.GetFragmentView<FAsceGridFootprintFragment>();
		for (const FAsceGridFootprintFragment& Footprint : Footprints)
		{
			ApplyBuildingFootprint(*LayeredCostField, *WorldMapData, Footprint, kNoBuildingCost);
		}
	});
}
