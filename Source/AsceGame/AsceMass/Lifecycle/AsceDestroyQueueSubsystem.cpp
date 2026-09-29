// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Lifecycle/AsceDestroyQueueSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassReplicationFragments.h"

#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Lifecycle/AsceDeathPresentation.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"
#include "GameModes/AsceGameState.h"
#include "Player/AscePlayerController.h"
#include "Teams/AsceTeamAgentInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceDestroyQueueSubsystem)

void UAsceDestroyQueueSubsystem::Enqueue(const FMassEntityHandle Entity)
{
	if (Entity.IsValid())
	{
		PendingDestroy.Add(Entity);
	}
}

void UAsceDestroyQueueSubsystem::RunDestroyQueueStage()
{
	if (PendingDestroy.Num() == 0)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)   // 只权威端
	{
		return;
	}
	UMassEntitySubsystem* Mass = World->GetSubsystem<UMassEntitySubsystem>();
	if (!Mass)
	{
		return;
	}
	FMassEntityManager& EntityManager = Mass->GetMutableEntityManager();

	// 视野判定：每个 controller 备一份阵营键（循环外一次；`IsEntityVisibleToLocalTeam` 与复制过滤同一口径）
	UAsceFogSubsystem* Fog = World->GetSubsystem<UAsceFogSubsystem>();
	AAsceGameState* GameState = World->GetGameState<AAsceGameState>();

	struct FClientInfo
	{
		APlayerController* PC = nullptr;
		int32 TeamKey = 0;
	};
	TArray<FClientInfo> Clients;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(PC);
		const int32 TeamId = TeamAgent ? AsceNormalizeTeamId(GenericTeamIdToInteger(TeamAgent->GetGenericTeamId())) : AsceNeutralTeamId;
		// 与复制过滤同一口径：打包 TeamKey（玩家阵营早已登记，等价于 GetTeamKey）
		Clients.Add({ PC, GameState ? GameState->GetOrAssignTeamKey(TeamId) : AsceNeutralTeamId });
	}

	// 本机 PC（standalone / 听服 host）：引擎不会给自己发 Client RPC ⇒ 直接在本端跑一遍"本地死亡表现"
	const APlayerController* LocalPC = GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;

	// 远端 controller 的死亡 NetID 数组 + 本机 PC 的实体列表
	TMap<APlayerController*, TArray<FMassNetworkID>> ClientNetIDs;
	TArray<FMassEntityHandle> LocalDeathEntities;

	const int32 Count = FMath::Min(PendingDestroy.Num(), MaxPerFrame);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FMassEntityHandle Entity = PendingDestroy[Index];
		if (!EntityManager.IsEntityValid(Entity))
		{
			continue;
		}
		if (FAsceLifecycleFragment* Lifecycle = EntityManager.GetFragmentDataPtr<FAsceLifecycleFragment>(Entity))
		{
			Lifecycle->SetDestroyRequested(true);   // ⇒ 复制侧 LOD Off ⇒ 各 bubble Remove ⇒ 引用计数归 0 ⇒ 销毁阶段销毁
		}
		else
		{
			continue;
		}

		const FMassNetworkIDFragment* NetIDFragment = EntityManager.GetFragmentDataPtr<FMassNetworkIDFragment>(Entity);
		if (!NetIDFragment)
		{
			continue;
		}

		for (const FClientInfo& Client : Clients)
		{
			if (!Client.PC)
			{
				continue;
			}
			// 没有 Fog（未就绪/单机）⇒ 保守全发（接收方按 NetID 自判有没有实体）
			if (Fog && !Fog->IsEntityVisibleToLocalTeam(EntityManager, Entity, Client.TeamKey))
			{
				continue;
			}
			if (Client.PC == LocalPC)
			{
				LocalDeathEntities.Add(Entity);   // 本机 PC：不走网络，直接本地表现
			}
			else
			{
				ClientNetIDs.FindOrAdd(Client.PC).Add(NetIDFragment->NetID);
			}
		}
	}
	PendingDestroy.RemoveAt(0, Count);

	// 本机 PC：直接本地表现（隐藏 + 死亡特效），和远端客户端看到的保持一致
	for (const FMassEntityHandle Entity : LocalDeathEntities)
	{
		AsceDeath::PlayLocalDeath(*World, Entity);
	}

	// 其余 controller：一 controller 一条 RPC（NetID 数组）；RPC 先于属性复制到达 ⇒ 客户端查实体时它还在
	for (const TPair<APlayerController*, TArray<FMassNetworkID>>& Pair : ClientNetIDs)
	{
		if (Pair.Value.Num() == 0)
		{
			continue;
		}
		if (AAscePlayerController* PC = Cast<AAscePlayerController>(Pair.Key))
		{
			PC->Client_NotifyDeaths(Pair.Value);
		}
	}
}
