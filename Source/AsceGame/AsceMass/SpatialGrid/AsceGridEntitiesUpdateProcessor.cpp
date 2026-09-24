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
		const int32 Num = Ctx.GetNumEntities();
		UAsceSpatialGridSubsystem& Grid = Ctx.GetMutableSubsystemChecked<UAsceSpatialGridSubsystem>();
		for (int32 i = 0; i < Num; ++i)
		{
			// **本端**阵营列：fragment 高 16 位是**写入方**的 index，必须用本端阵营表重映射
			Grid.InsertEntity(Handles[i], Transforms[i].GetTransform().GetLocation(), RadiusList[i].Radius, Heights[i].Height,
				GameState ? GameState->FindTeamIndex(AsceTeamId(Teams[i].TeamKey)) : 0);
		}
	});
}