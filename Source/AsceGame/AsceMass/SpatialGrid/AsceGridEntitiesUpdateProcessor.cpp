// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceGridEntitiesUpdateProcessor.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "Engine/World.h"          // GetWorld()->GetGameState<>()
#include "../SpatialGrid/AsceSpatialGridSubsystem.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "GameModes/AsceGameState.h"   // 本端阵营表（TeamId → 本端 index）

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceGridEntitiesUpdateProcessor)

UAsceGridEntitiesUpdateProcessor::UAsceGridEntitiesUpdateProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = false;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
}

void UAsceGridEntitiesUpdateProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddTagRequirement<FAsceMovableUnitTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAsceHeightFragment>(EMassFragmentAccess::ReadOnly);
	// 阵营：决定写入格子的"阵营列"（写入与移除共用 AsceTeamIndex；缺该 fragment 的实体会从单位层消失，
	// 所以这是硬要求——单位由 UAsceRTSEntityTrait 提供、工厂有 ensure 兜）
	EntityQuery.AddRequirement<FAsceTeamFragment>(EMassFragmentAccess::ReadOnly);
	// 生命周期：**待销毁（Dying）的实体视为已销毁**，不进网格（见下）——空间层是选择/寻路/放置/战斗的公共底，
	// 在真正销毁前的 1~3 帧里，所有消费方**都应该看不到它**。核心 trait 恒挂，用 Optional 保险。
	EntityQuery.AddRequirement<FAsceLifecycleFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery.AddSubsystemRequirement<UAsceSpatialGridSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceGridEntitiesUpdateProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 本端阵营表（GameState）在进并行前取一次：定长、永不 realloc ⇒ 并行内只读安全
	const AAsceGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AAsceGameState>() : nullptr;
	EntityQuery.ParallelForEachEntityChunk(Context, [GameState](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FAgentRadiusFragment> RadiusList = Ctx.GetFragmentView<FAgentRadiusFragment>();
		const TConstArrayView<FMassEntityHandle> Handles = Ctx.GetEntities();
		const TConstArrayView<FAsceHeightFragment> Heights = Ctx.GetFragmentView<FAsceHeightFragment>();
		const TConstArrayView<FAsceTeamFragment> Teams = Ctx.GetFragmentView<FAsceTeamFragment>();
		const TConstArrayView<FAsceLifecycleFragment> Lifecycles = Ctx.GetFragmentView<FAsceLifecycleFragment>();
		const int32 Num = Ctx.GetNumEntities();
		UAsceSpatialGridSubsystem& Grid = Ctx.GetMutableSubsystemChecked<UAsceSpatialGridSubsystem>();
		for (int32 i = 0; i < Num; ++i)
		{
			// 待销毁（`RequestDestroy` / 战斗死亡当场置 Dying）**视为已销毁**：不进网格。
			// 真正销毁在帧首销毁阶段（引用计数归零后），那之前的 1~3 帧里所有消费方都不该再看到它。
			if (Lifecycles.Num() == Num && Lifecycles[i].IsDying())
			{
				continue;
			}
			// **本端**阵营列：fragment 高 16 位是**写入方**的 index，必须用本端阵营表重映射
			Grid.InsertEntity(Handles[i], Transforms[i].GetTransform().GetLocation(), RadiusList[i].Radius, Heights[i].Height,
				GameState ? GameState->FindTeamIndex(AsceTeamId(Teams[i].TeamKey)) : 0);
		}
	});
}
