// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceVisionViewRemoveObserverProcessor.h"

#include "MassExecutionContext.h"

#include "AsceMass/Vision/AsceVisionFragments.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceVisionViewRemoveObserverProcessor)

UAsceVisionViewRemoveObserverProcessor::UAsceVisionViewRemoveObserverProcessor()
	: EntityQuery(*this)
{
	ObservedType = FAsceVisionViewFragment::StaticStruct();
	ObservedOperations = EMassObservedOperationFlags::Remove;
	// Server/Client 都维护各自的贡献表（Server 过滤 / Client 表现），单位消亡都要清理
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
}

void UAsceVisionViewRemoveObserverProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FAsceVisionViewFragment>(EMassFragmentAccess::ReadOnly);
	// 上帧贡献状态：单位销毁前仍可读，用它对"旧参数"下线（Any 防只有 ViewFragment 的实体/顺序差异）
	EntityQuery.AddRequirement<FAsceVisionFogStateFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Any);
	EntityQuery.AddSubsystemRequirement<UAsceFogSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceVisionViewRemoveObserverProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
	{
		UAsceFogSubsystem& Fog = Ctx.GetMutableSubsystemChecked<UAsceFogSubsystem>();
		if (!Fog.IsReady())
		{
			return; // 地图尚未初始化：本帧不会有过贡献的单位，无需清理
		}

		if (!Ctx.DoesArchetypeHaveFragment<FAsceVisionFogStateFragment>())
		{
			return; // 这类实体从未做过贡献（理论上不会发生，防御）
		}

		const TConstArrayView<FAsceVisionFogStateFragment> PrevCells = Ctx.GetFragmentView<FAsceVisionFogStateFragment>();
		for (const FAsceVisionFogStateFragment& Prev : PrevCells)
		{
			if (Prev.LastVisionCellIndex >= 0)
			{
				Fog.RemoveContribution(Prev.LastTeamKey, Prev.LastVisionCellIndex, Prev.LastHe, Prev.LastR);
			}
		}
	});
}
