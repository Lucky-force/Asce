// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsceGameState.h"

#include "AbilitySystem/AsceAbilitySystemComponent.h"
#include "Async/TaskGraphInterfaces.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameModes/AsceExperienceManagerComponent.h"
#include "Messages/AsceVerbMessage.h"
#include "Player/AscePlayerState.h"
#include "Teams/AsceTeamAgentInterface.h"   // GenericTeamIdToInteger（本端阵营表用玩家 TeamId）
#include "AsceLogChannels.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceGameState)

class APlayerState;
class FLifetimeProperty;

extern ENGINE_API float GAverageFPS;


AAsceGameState::AAsceGameState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	AbilitySystemComponent = ObjectInitializer.CreateDefaultSubobject<UAsceAbilitySystemComponent>(this, TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	ExperienceManagerComponent = CreateDefaultSubobject<UAsceExperienceManagerComponent>(TEXT("ExperienceManagerComponent"));

	ServerFPS = 0.0f;
}

void AAsceGameState::PreInitializeComponents()
{
	Super::PreInitializeComponents();
}

void AAsceGameState::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	check(AbilitySystemComponent);
	AbilitySystemComponent->InitAbilityActorInfo(/*Owner=*/ this, /*Avatar=*/ this);
}

UAbilitySystemComponent* AAsceGameState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AAsceGameState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void AAsceGameState::AddPlayerState(APlayerState* PlayerState)
{
	Super::AddPlayerState(PlayerState);

	// 本端阵营表：玩家队**先**登记（index 稳定，不会因为某个 AI 先刷出来而抖动）。
	// 服务器与各客户端各自登记自己的表（TeamId 一致，TeamIndex 各端可以不同）。
	SeedLocalTeamsFromPlayers();
}

void AAsceGameState::RemovePlayerState(APlayerState* PlayerState)
{
	//@TODO: This isn't getting called right now (only the 'rich' AGameMode uses it, not AGameModeBase)
	// Need to at least comment the engine code, and possibly move things around
	Super::RemovePlayerState(PlayerState);
	// 注意：**不回收**阵营表槽位——index 必须在一局内保持稳定（实体 fragment 里的打包值、列/行下标都依赖它）。
}

// ──────────────────────────────────────────
// 本端阵营表（TeamId → TeamIndex）
// ──────────────────────────────────────────

int32 AAsceGameState::GetOrAssignTeamIndex(int32 RawTeamId)
{
	// 表的**唯一写入点**：只在 GameThread 调用（实体创建、PlayerState 到达）
	check(IsInGameThread());

	const int32 TeamId = AsceNormalizeTeamId(RawTeamId);
	if (TeamId == AsceNeutralTeamId)
	{
		return 0;
	}

	for (int32 Index = 1; Index < LocalTeamCount; ++Index)
	{
		if (LocalTeamIds[Index] == TeamId)
		{
			return Index;
		}
	}

	if (LocalTeamCount >= AsceMaxTeams)
	{
		if (!bLoggedLocalTeamsFull)
		{
			bLoggedLocalTeamsFull = true;
			UE_LOG(LogAsceTeams, Warning, TEXT("[AsceGameState] 本端阵营表已满（%d 队），TeamId=%d 归入中立列"), AsceMaxTeams, TeamId);
		}
		return 0;
	}

	const int32 NewIndex = LocalTeamCount++;
	LocalTeamIds[NewIndex] = TeamId;
	return NewIndex;
}

int32 AAsceGameState::FindTeamIndex(int32 RawTeamId) const
{
	const int32 TeamId = AsceNormalizeTeamId(RawTeamId);
	if (TeamId == AsceNeutralTeamId)
	{
		return 0;
	}
	for (int32 Index = 1; Index < LocalTeamCount; ++Index)
	{
		if (LocalTeamIds[Index] == TeamId)
		{
			return Index;
		}
	}
	// 未登记（并行路径的正常兜底）：中立列
	return 0;
}

int32 AAsceGameState::GetTeamKey(int32 RawTeamId) const
{
	const int32 TeamId = AsceNormalizeTeamId(RawTeamId);
	if (TeamId == AsceNeutralTeamId)
	{
		return 0;
	}
	return AsceMakeTeamKey(FindTeamIndex(TeamId), TeamId);
}

void AAsceGameState::SeedLocalTeamsFromPlayers()
{
	check(IsInGameThread());

	for (APlayerState* PlayerState : PlayerArray)
	{
		if (const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(PlayerState))
		{
			GetOrAssignTeamIndex(GenericTeamIdToInteger(TeamAgent->GetGenericTeamId()));
		}
	}
}

void AAsceGameState::SeamlessTravelTransitionCheckpoint(bool bToTransitionMap)
{
	// Remove inactive and bots
	// 在SeamlessTravel的tick中调用，清除非活跃状态的玩家和bot
	for (int32 i = PlayerArray.Num() - 1; i >= 0; i--)
	{
		APlayerState* PlayerState = PlayerArray[i];
		if (PlayerState && (PlayerState->IsABot() || PlayerState->IsInactive()))
		{
			RemovePlayerState(PlayerState);
		}
	}
}

void AAsceGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, ServerFPS);
	DOREPLIFETIME_CONDITION(ThisClass, RecorderPlayerState, COND_ReplayOnly);
}

void AAsceGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (GetLocalRole() == ROLE_Authority)
	{
		ServerFPS = GAverageFPS;
	}
}

void AAsceGameState::MulticastMessageToClients_Implementation(const FAsceVerbMessage Message)
{
	if (GetNetMode() == NM_Client)
	{
		UGameplayMessageSubsystem::Get(this).BroadcastMessage(Message.Verb, Message);
	}
}

void AAsceGameState::MulticastReliableMessageToClients_Implementation(const FAsceVerbMessage Message)
{
	MulticastMessageToClients_Implementation(Message);
}

float AAsceGameState::GetServerFPS() const
{
	return ServerFPS;
}

void AAsceGameState::SetRecorderPlayerState(APlayerState* NewPlayerState)
{
	if (RecorderPlayerState == nullptr)
	{
		// Set it and call the rep callback so it can do any record-time setup
		RecorderPlayerState = NewPlayerState;
		OnRep_RecorderPlayerState();
	}
	else
	{
		UE_LOG(LogAsce, Warning, TEXT("SetRecorderPlayerState was called on %s but should only be called once per game on the primary user"), *GetName());
	}
}

APlayerState* AAsceGameState::GetRecorderPlayerState() const
{
	// TODO: Maybe auto select it if null?

	return RecorderPlayerState;
}

void AAsceGameState::OnRep_RecorderPlayerState()
{
	OnRecorderPlayerStateChangedEvent.Broadcast(RecorderPlayerState);
}