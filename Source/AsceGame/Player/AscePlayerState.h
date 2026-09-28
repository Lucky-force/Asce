// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AbilitySystemInterface.h"
#include "ModularPlayerState.h"
#include "System/GameplayTagStack.h"
#include "Teams/AsceTeamAgentInterface.h"

#include "AscePlayerState.generated.h"

#define UE_API ASCEGAME_API

struct FAsceVerbMessage;

class AController;
class AAscePlayerController;
class APlayerState;
class FName;
class UAbilitySystemComponent;
class UAsceAbilitySystemComponent;
class UAsceExperienceDefinition;
class UAscePawnData;
class UObject;
struct FFrame;
struct FGameplayTag;

/** Defines the types of client connected */
// 定义客户端连接类型
UENUM()
enum class EAscePlayerConnectionType : uint8
{
	// An active player
	// 活跃玩家
	Player = 0,

	// Spectator connected to a running game
	// 连接到运行中游戏的观察者
	LiveSpectator,

	// Spectating a demo recording offline
	// 离线观看demo记录的观察者
	ReplaySpectator,

	// A deactivated player (disconnected)
	// 暂时断开连接的玩家
	InactivePlayer
};

/**
 * AAscePlayerState
 *
 *	Base player state class used by this project.
 *	记录玩家的状态，服务端创建，同步数据给所有客户端
 */
UCLASS(MinimalAPI, Config = Game)
class AAscePlayerState : public AModularPlayerState, public IAbilitySystemInterface, public IAsceTeamAgentInterface
{
	GENERATED_BODY()

public:
	UE_API AAscePlayerState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, Category = "Asce|PlayerState")
	UE_API AAscePlayerController* GetAscePlayerController() const;

	UFUNCTION(BlueprintCallable, Category = "Asce|PlayerState")
	UAsceAbilitySystemComponent* GetAsceAbilitySystemComponent() const { return AbilitySystemComponent; }
	UE_API virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	template <class T>
	const T* GetPawnData() const { return Cast<T>(PawnData); }

	UE_API void SetPawnData(const UAscePawnData* InPawnData);

	//~AActor interface
	UE_API virtual void PreInitializeComponents() override;
	UE_API virtual void PostInitializeComponents() override;
	//~End of AActor interface

	//~APlayerState interface
	UE_API virtual void Reset() override;
	UE_API virtual void ClientInitialize(AController* C) override;
	UE_API virtual void CopyProperties(APlayerState* PlayerState) override;
	UE_API virtual void OnDeactivated() override;
	UE_API virtual void OnReactivated() override;
	//~End of APlayerState interface

	//~IAsceTeamAgentInterface interface
	UE_API virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	UE_API virtual FGenericTeamId GetGenericTeamId() const override;
	UE_API virtual FOnAsceTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() override;
	//~End of IAsceTeamAgentInterface interface

	/** 把队伍登记进本端阵营表，并（若是本地玩家）刷新黑雾渲染使用的队伍行。
	 *  设队/OnRep 时调用；本地玩家被附身时也调一次（兜"设队早于附身"）。 */
	UE_API void RefreshLocalFogTeamKey();

	static UE_API const FName NAME_AsceAbilityReady;

	UE_API void SetPlayerConnectionType(EAscePlayerConnectionType NewType);
	EAscePlayerConnectionType GetPlayerConnectionType() const { return MyPlayerConnectionType; }

	/** Returns the Squad ID of the squad the player belongs to. */
	UFUNCTION(BlueprintCallable)
	int32 GetSquadId() const
	{
		return MySquadID;
	}

	/** Returns the Team ID of the team the player belongs to. */
	UFUNCTION(BlueprintCallable)
	int32 GetTeamId() const
	{
		return GenericTeamIdToInteger(MyTeamID);
	}

	UE_API void SetSquadID(int32 NewSquadID);

	// Adds a specified number of stacks to the tag (does nothing if StackCount is below 1)
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Teams)
	UE_API void AddStatTagStack(FGameplayTag Tag, int32 StackCount);

	// Removes a specified number of stacks from the tag (does nothing if StackCount is below 1)
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Teams)
	UE_API void RemoveStatTagStack(FGameplayTag Tag, int32 StackCount);

	// Returns the stack count of the specified tag (or 0 if the tag is not present)
	UFUNCTION(BlueprintCallable, Category=Teams)
	UE_API int32 GetStatTagStackCount(FGameplayTag Tag) const;

	// Returns true if there is at least one stack of the specified tag
	UFUNCTION(BlueprintCallable, Category=Teams)
	UE_API bool HasStatTag(FGameplayTag Tag) const;

	// Send a message to just this player
	// (use only for client notifications like accolades, quest toasts, etc... that can handle being occasionally lost)
	UFUNCTION(Client, Unreliable, BlueprintCallable, Category = "Asce|PlayerState")
	UE_API void ClientBroadcastMessage(const FAsceVerbMessage Message);

	// Gets the replicated view rotation of this player, used for spectating
	UE_API FRotator GetReplicatedViewRotation() const;

	// Sets the replicated view rotation, only valid on the server
	UE_API void SetReplicatedViewRotation(const FRotator& NewRotation);

private:
	UE_API void OnExperienceLoaded(const UAsceExperienceDefinition* CurrentExperience);

protected:
	UFUNCTION()
	UE_API void OnRep_PawnData();

protected:

	UPROPERTY(ReplicatedUsing = OnRep_PawnData)
	TObjectPtr<const UAscePawnData> PawnData;

private:

	// The ability system component sub-object used by player characters.
	// 玩家的能力组件放在PlayerState上面，所有玩家的playerstate都将同步到客户端
	UPROPERTY(VisibleAnywhere, Category = "Asce|PlayerState")
	TObjectPtr<UAsceAbilitySystemComponent> AbilitySystemComponent;

	// Health attribute set used by this actor.
	UPROPERTY()
	TObjectPtr<const class UAsceHealthSet> HealthSet;
	// Combat attribute set used by this actor.
	UPROPERTY()
	TObjectPtr<const class UAsceCombatSet> CombatSet;

	UPROPERTY(Replicated)
	EAscePlayerConnectionType MyPlayerConnectionType;

	UPROPERTY()
	FOnAsceTeamIndexChangedDelegate OnTeamChangedDelegate;

	UPROPERTY(ReplicatedUsing=OnRep_MyTeamID)
	FGenericTeamId MyTeamID;

	UPROPERTY(ReplicatedUsing=OnRep_MySquadID)
	int32 MySquadID;

	UPROPERTY(Replicated)
	FGameplayTagStackContainer StatTags;

	UPROPERTY(Replicated)
	FRotator ReplicatedViewRotation;

private:
	UFUNCTION()
	UE_API void OnRep_MyTeamID(FGenericTeamId OldTeamID);

	UFUNCTION()
	UE_API void OnRep_MySquadID();
};

#undef UE_API
