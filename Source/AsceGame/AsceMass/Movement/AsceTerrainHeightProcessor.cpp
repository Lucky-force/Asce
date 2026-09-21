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
	// 动态处理器：由 UAsceMassMovementSimulationSubsystem 手动按序跑（调度器是唯一时钟）；
	// 默认 true 会被 MassSimulation 自动链再跑一份，且绕过 world init/loading 门控。
	bAutoRegisterWithProcessingPhases = false;
	// Server和Client都要做：两端各自采样同一份静态高度场（确定性一致），Z 随位置一起复制
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	// 不设ExecuteInGroup：由 SimulationSubsystem 手动按序编排（CollisionSolver 之后、Orientation 之前）
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
}

void UAsceTerrainHeightProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	// **查询只要 Transform** ⇒ 所有"需要贴地"的实体都走这一份实现：单位（移动管线里）与建筑（静态，
	// 每帧被同一次遍历采样，幂等）共用；**创建方不要自己采样高度**，否则就是两套口径。
	// TODO(贴地标记)：以后如果有"不该贴地"的实体（飞行单位/空中投射物），需要加一个标记 tag
	// （例如由地面单位/建筑 trait 添加的 `FAsceTerrainSnapTag`）来排除它们。
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	// 只读高度场（trait GameThreadOnly=false，只有 StaticHeights 并行安全）
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
		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			FTransform& Transform = Transforms[i].GetMutableTransform();
			const FVector Location = Transform.GetLocation();

			// 双线性采样（与其它贴地消费者共用同一份实现；图外不贴地、保持原 Z）
			float Height = 0.f;
			if (HMap.SampleHeightWorld(FVector2D(Location.X, Location.Y), Height))
			{
				Transform.SetTranslation(FVector(Location.X, Location.Y, Height));
			}
		}
	});
}

