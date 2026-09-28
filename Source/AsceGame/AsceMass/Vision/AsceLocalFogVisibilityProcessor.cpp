// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceLocalFogVisibilityProcessor.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "MassEntityManager.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassRepresentationFragments.h"
#include "MassRepresentationProcessor.h"
#include "MassVisualizationLODProcessor.h"

#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"
#include "GameModes/AsceGameState.h"
#include "Teams/AsceTeamAgentInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceLocalFogVisibilityProcessor)

UAsceLocalFogVisibilityProcessor::UAsceLocalFogVisibilityProcessor()
	: EntityQuery(*this)
{
	// 表现层处理器：走引擎标准处理链（可视化处理器都在那里）
	bAutoRegisterWithProcessingPhases = true;
	// 只在 standalone / 听服 host 跑（客户端由复制裁剪负责；专用服务器无本地玩家，Execute 里早退）
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);

	// 顺序必须卡在"距离 LOD 算完"和"决定 representation"之间：
	// 否则会被距离 LOD 覆盖回去（闪一帧）或晚一帧才隐藏。
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Representation;
	ExecutionOrder.ExecuteAfter.Add(UMassVisualizationLODProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteBefore.Add(UMassVisualizationProcessor::StaticClass()->GetFName());
}

void UAsceLocalFogVisibilityProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	// 只需要表现 LOD（有它 ⇒ 实体挂了可视化 trait）；几何/队伍由 Fog 的便捷判定自己按需取
	EntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadWrite);
}

void UAsceLocalFogVisibilityProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const UWorld* World = Context.GetWorld();
	if (!World)
	{
		return;
	}

	// 本地玩家队伍（专用服务器/观战无本地玩家 → 不做）
	APlayerController* PC = GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	IGenericTeamAgentInterface* TeamAgent = PC ? Cast<IGenericTeamAgentInterface>(PC) : nullptr;
	if (!TeamAgent)
	{
		return;
	}
	const int32 TeamId = AsceNormalizeTeamId(GenericTeamIdToInteger(TeamAgent->GetGenericTeamId()));
	if (TeamId == AsceNeutralTeamId)
	{
		return;
	}

	UAsceFogSubsystem* Fog = UAsceFogSubsystem::Get(World);
	AAsceGameState* GameState = World->GetGameState<AAsceGameState>();
	if (!Fog || !Fog->IsReady() || !GameState)
	{
		return;
	}
	const int32 TeamKey = GameState->GetTeamKey(TeamId);
	if (TeamKey == 0)
	{
		return;   // 本端队伍未登记：不裁（安全默认）
	}

	EntityQuery.ForEachEntityChunk(Context, [&EntityManager, Fog, TeamKey](FMassExecutionContext& Ctx)
	{
		const TArrayView<FMassRepresentationLODFragment> LODList = Ctx.GetMutableFragmentView<FMassRepresentationLODFragment>();
		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			// 距离剔除已判 Off 的直接跳过（本来就不渲染，也省掉迷雾查询）
			if (LODList[i].LOD >= EMassLOD::Off)
			{
				continue;
			}

			const FMassEntityHandle Entity = Ctx.GetEntity(i);
			if (!Fog->IsEntityVisibleToLocalTeam(EntityManager, Entity, TeamKey))
			{
				LODList[i].LOD = EMassLOD::Off;   // 视野剔除：只影响表现（复制用的是另一个 fragment）
			}
		}
	});
}
