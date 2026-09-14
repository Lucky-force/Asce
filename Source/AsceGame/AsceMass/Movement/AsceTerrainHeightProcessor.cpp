// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceTerrainHeightProcessor.h"

#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceTerrainHeightProcessor)

UAsceTerrainHeightProcessor::UAsceTerrainHeightProcessor()
	: EntityQuery(*this)
{
	// Server和Client都要做：两端各自采样同一份静态高度场（确定性一致），Z 随位置一起复制
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	// 不设ExecuteInGroup：由 SimulationSubsystem 手动按序编排（CollisionSolver 之后、Orientation 之前）
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
}

void UAsceTerrainHeightProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	// 只读高度场（trait GameThreadOnly=false，只读 StaticHeights 并行安全）
	EntityQuery.AddSubsystemRequirement<UAsceFogSubsystem>(EMassFragmentAccess::ReadOnly);
}

void UAsceTerrainHeightProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ParallelForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
	{
		const UAsceFogSubsystem& Fog = Ctx.GetSubsystemChecked<UAsceFogSubsystem>();
		const FAsceVisionHeightMap& HMap = Fog.GetHeightMap();
		if (!HMap.IsValid())
		{
			return; // 高度场未就绪（加载期），跳过
		}

		const TArrayView<FTransformFragment> Transforms = Ctx.GetMutableFragmentView<FTransformFragment>();
		const int32 GridW = HMap.GetGridW();
		const int32 GridH = HMap.GetGridH();
		const float CellCm = HMap.GetCellSizeCm();
		if (GridW <= 0 || GridH <= 0 || CellCm <= 0.f)
		{
			return;
		}

		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			FTransform& Transform = Transforms[i].GetMutableTransform();
			const FVector Location = Transform.GetLocation();

			// 世界 → 格中心网格坐标（格中心 = 整数）
			const float GX = static_cast<float>(Location.X) / CellCm - 0.5f;
			const float GY = static_cast<float>(Location.Y) / CellCm - 0.5f;

			// 图外：不做贴地，保持原 Z
			if (GX < -0.5f || GY < -0.5f || GX > GridW - 0.5f || GY > GridH - 0.5f)
			{
				continue;
			}

			const int32 X0 = FMath::FloorToInt32(GX);
			const int32 Y0 = FMath::FloorToInt32(GY);
			const float FX = GX - static_cast<float>(X0);
			const float FY = GY - static_cast<float>(Y0);

			// 采样点 clamp 到图内（边缘安全；图内 2×2 采样）
			const int32 X0C = FMath::Clamp(X0, 0, GridW - 1);
			const int32 Y0C = FMath::Clamp(Y0, 0, GridH - 1);
			const int32 X1C = FMath::Clamp(X0 + 1, 0, GridW - 1);
			const int32 Y1C = FMath::Clamp(Y0 + 1, 0, GridH - 1);

			// 双线性：先水平（两行各插值）后垂直
			const float H00 = static_cast<float>(HMap.GetStaticHeight(X0C + Y0C * GridW));
			const float H10 = static_cast<float>(HMap.GetStaticHeight(X1C + Y0C * GridW));
			const float H01 = static_cast<float>(HMap.GetStaticHeight(X0C + Y1C * GridW));
			const float H11 = static_cast<float>(HMap.GetStaticHeight(X1C + Y1C * GridW));
			const float Bottom = FMath::Lerp(H00, H10, FX);
			const float Top = FMath::Lerp(H01, H11, FX);
			const float Height = FMath::Lerp(Bottom, Top, FY);

			Transform.SetTranslation(FVector(Location.X, Location.Y, Height));
		}
	});
}
