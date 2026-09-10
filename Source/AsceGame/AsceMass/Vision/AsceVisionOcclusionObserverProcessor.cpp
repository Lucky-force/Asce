// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceVisionOcclusionObserverProcessor.h"

#include "MassExecutionContext.h"

#include "AsceMass/Fragments/AsceFootprintFragment.h"
#include "AsceMass/Vision/AsceVisionDefines.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceVisionOcclusionObserverProcessor)

namespace
{
	/** 把 flowfield footprint 换算成视野格矩形，返回是否有合法区域。 */
	bool ConvertFootprintToVisionRect(const FAsceGridFootprintFragment& Footprint, float CellSizeCm, FAsceOcclusionRangeWrite& OutRect)
	{
		if (Footprint.SizeX <= 0 || Footprint.SizeY <= 0)
		{
			return false;
		}
		const int32 Scale = FMath::RoundToInt32(AsceVision::FlowFieldGridSizeCm / CellSizeCm);
		OutRect.X0 = Footprint.OriginGridLocation.X * Scale;
		OutRect.Y0 = Footprint.OriginGridLocation.Y * Scale;
		OutRect.X1 = OutRect.X0 + Footprint.SizeX * Scale - 1;
		OutRect.Y1 = OutRect.Y0 + Footprint.SizeY * Scale - 1;
		return true;
	}
}

// ────────────────────────────────────────────
// Add（建筑入场：写遮挡）
// ────────────────────────────────────────────

UAsceVisionOcclusionAddObserverProcessor::UAsceVisionOcclusionAddObserverProcessor()
	: EntityQuery(*this)
{
	// 观察 footprint：挡视野 = 挂 footprint 的建筑（遮挡高在 OcclusionHeightCm）
	ObservedType = FAsceGridFootprintFragment::StaticStruct();
	ObservedOperations = EMassObservedOperationFlags::Add;
	// Server 权威侧才需要维护动态遮挡层（过滤用）；Client 不在这里重建（走复制通道）
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
}

void UAsceVisionOcclusionAddObserverProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FAsceGridFootprintFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UAsceFogSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceVisionOcclusionAddObserverProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
	{
		UAsceFogSubsystem& Fog = Ctx.GetMutableSubsystemChecked<UAsceFogSubsystem>();
		if (!Fog.IsReady())
		{
			return; // 高度场尚未初始化
		}

		const TConstArrayView<FAsceGridFootprintFragment> Footprints = Ctx.GetFragmentView<FAsceGridFootprintFragment>();
		const float CellSize = Fog.GetHeightMap().GetCellSizeCm();

		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			if (Footprints[i].OcclusionHeightCm == 0)
			{
				continue; // 这块 footprint 不挡视野（如部分地物），无需写遮挡
			}

			FAsceOcclusionRangeWrite Rect;
			if (ConvertFootprintToVisionRect(Footprints[i], CellSize, Rect))
			{
				Rect.OcclusionHeightCm = Footprints[i].OcclusionHeightCm;
				Fog.AddOcclusionRect(Rect);
			}
		}
	});
}

// ────────────────────────────────────────────
// Remove（建筑退场：清遮挡）
// ────────────────────────────────────────────

UAsceVisionOcclusionRemoveObserverProcessor::UAsceVisionOcclusionRemoveObserverProcessor()
	: EntityQuery(*this)
{
	ObservedType = FAsceGridFootprintFragment::StaticStruct();
	ObservedOperations = EMassObservedOperationFlags::Remove;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
}

void UAsceVisionOcclusionRemoveObserverProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FAsceGridFootprintFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UAsceFogSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceVisionOcclusionRemoveObserverProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
	{
		UAsceFogSubsystem& Fog = Ctx.GetMutableSubsystemChecked<UAsceFogSubsystem>();
		if (!Fog.IsReady())
		{
			return;
		}

		const TConstArrayView<FAsceGridFootprintFragment> Footprints = Ctx.GetFragmentView<FAsceGridFootprintFragment>();
		const float CellSize = Fog.GetHeightMap().GetCellSizeCm();

		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			if (Footprints[i].OcclusionHeightCm == 0)
			{
				continue; // 本来就没写遮挡，无需清
			}

			FAsceOcclusionRangeWrite Rect;
			if (ConvertFootprintToVisionRect(Footprints[i], CellSize, Rect))
			{
				Fog.RemoveOcclusionRect(Rect);
			}
		}
	});
}
