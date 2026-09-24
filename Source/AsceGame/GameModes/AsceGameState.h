// Fill out your copyright notice in the Description page of Project Settings.

#pragma once


#include "AbilitySystemInterface.h"
#include "ModularGameState.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"   // 阵营口径（AsceMaxTeams / AsceTeamId / AsceTeamIndex / AsceMakeTeamKey）

#include "AsceGameState.generated.h"

#define UE_API ASCEGAME_API

struct FAsceVerbMessage;

class APlayerState;
class UAbilitySystemComponent;
class UAsceAbilitySystemComponent;
class UAsceExperienceManagerComponent;
class UObject;
struct FFrame;

/**
 * AAsceGameState
 *
 *	The base game state class used by this project.
 *	服务器创建的权威数据
 */
UCLASS(MinimalAPI, Config = Game)
class AAsceGameState : public AModularGameStateBase, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:

	UE_API AAsceGameState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~AActor interface
	UE_API virtual void PreInitializeComponents() override;
	UE_API virtual void PostInitializeComponents() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UE_API virtual void Tick(float DeltaSeconds) override;
	//~End of AActor interface

	//~AGameStateBase interface
	UE_API virtual void AddPlayerState(APlayerState* PlayerState) override;
	UE_API virtual void RemovePlayerState(APlayerState* PlayerState) override;
	UE_API virtual void SeamlessTravelTransitionCheckpoint(bool bToTransitionMap) override;
	//~End of AGameStateBase interface

	//~IAbilitySystemInterface
	UE_API virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End of IAbilitySystemInterface

	// Gets the ability system component used for game wide things
	UFUNCTION(BlueprintCallable, Category = "Asce|GameState")
	UAsceAbilitySystemComponent* GetLyraAbilitySystemComponent() const { return AbilitySystemComponent; }

	// Send a message that all clients will (probably) get
	// (use only for client notifications like eliminations, server join messages, etc... that can handle being lost)
	UFUNCTION(NetMulticast, Unreliable, BlueprintCallable, Category = "Asce|GameState")
	UE_API void MulticastMessageToClients(const FAsceVerbMessage Message);

	// Send a message that all clients will be guaranteed to get
	// (use only for client notifications that cannot handle being lost)
	UFUNCTION(NetMulticast, Reliable, BlueprintCallable, Category = "Asce|GameState")
	UE_API void MulticastReliableMessageToClients(const FAsceVerbMessage Message);

	// Gets the server's FPS, replicated to clients
	UE_API float GetServerFPS() const;

	// ── 本端阵营表（**不复制**：TeamIndex 是每端自己的、先来先得；TeamId 才是跨端一致的身份） ──
	// 定长 AsceMaxTeams 项、永不 realloc ⇒ 并行路径（空间层插入/查询、迷雾）可只读访问；
	// **分配只在 GameThread**（实体创建、PlayerState 到达）。
	// 槽位 = TeamIndex，值 = TeamId；槽 0 恒为中立（TeamId 0）。

	/** GameThread：登记 TeamId（首见分配 index）并返回其本端 index；表满 → 归 0 号中立列并报警一次 */
	UE_API int32 GetOrAssignTeamIndex(int32 RawTeamId);

	/** 只读（并行安全）：TeamId → 本端 index；未登记/中立 → 0 */
	UE_API int32 FindTeamIndex(int32 RawTeamId) const;

	/** 只读（并行安全）：TeamId → 打包 TeamKey（未登记/中立 → 0） */
	UE_API int32 GetTeamKey(int32 RawTeamId) const;

	/** 把 PlayerArray 里所有 PlayerState 的 TeamId 先登记（玩家 index 稳定，不随 AI 出现顺序抖动） */
	UE_API void SeedLocalTeamsFromPlayers();

	// Indicate the local player state is recording a replay
	UE_API void SetRecorderPlayerState(APlayerState* NewPlayerState);

	// Gets the player state that recorded the replay, if valid
	UE_API APlayerState* GetRecorderPlayerState() const;

	// Delegate called when the replay player state changes
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRecorderPlayerStateChanged, APlayerState*);
	FOnRecorderPlayerStateChanged OnRecorderPlayerStateChangedEvent;

private:
	// Handles loading and managing the current gameplay experience
	UPROPERTY()
	TObjectPtr<UAsceExperienceManagerComponent> ExperienceManagerComponent;

	// The ability system component subobject for game-wide things (primarily gameplay cues)
	// 用来管理游戏里面一些全局的特殊行为，比如cue
	UPROPERTY(VisibleAnywhere, Category = "Asce|GameState")
	TObjectPtr<UAsceAbilitySystemComponent> AbilitySystemComponent;

protected:
	UPROPERTY(Replicated)
	float ServerFPS;

	// 本端阵营表：槽位 = TeamIndex，值 = TeamId（槽 0 恒为中立）。**不复制**（每端自己的下标）。
	int32 LocalTeamIds[AsceMaxTeams] = {};
	int32 LocalTeamCount = 1;   // 0 号（中立）恒存在
	bool bLoggedLocalTeamsFull = false;

	// The player state that recorded a replay, it is used to select the right pawn to follow
	// This is only set in replay streams and is not replicated normally
	UPROPERTY(Transient, ReplicatedUsing = OnRep_RecorderPlayerState)
	TObjectPtr<APlayerState> RecorderPlayerState;

	UFUNCTION()
	UE_API void OnRep_RecorderPlayerState();

};

#undef UE_API
