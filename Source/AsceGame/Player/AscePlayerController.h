// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

//#include "Camera/AsceCameraAssistInterface.h"
#include "CommonPlayerController.h"
#include "Engine/NetSerialization.h"
#include "Teams/AsceTeamAgentInterface.h"

#include "AscePlayerController.generated.h"

#define UE_API ASCEGAME_API

struct FGenericTeamId;

class AAsceHUD;
class AAscePlayerState;
class APawn;
class APlayerState;
class FPrimitiveComponentId;
class IInputInterface;
class UAsceAbilitySystemComponent;
class UAsceSettingsShared;
class UObject;
class UPlayer;
struct FFrame;

/**
 * AAscePlayerController
 *
 *	The base player controller class used by this project.
 */
UCLASS(MinimalAPI, Config = Game, Meta = (ShortTooltip = "The base player controller class used by this project."))
class AAscePlayerController : public ACommonPlayerController, public IAsceTeamAgentInterface//, public IAsceCameraAssistInterface
{
	GENERATED_BODY()

public:

	UE_API AAscePlayerController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, Category = "Asce|PlayerController")
	UE_API AAscePlayerState* GetAscePlayerState() const;

	UFUNCTION(BlueprintCallable, Category = "Asce|PlayerController")
	UE_API UAsceAbilitySystemComponent* GetAsceAbilitySystemComponent() const;

	UFUNCTION(BlueprintCallable, Category = "Asce|PlayerController")
	UE_API AAsceHUD* GetAsceHUD() const;

	// Call from game state logic to start recording an automatic client replay if ShouldRecordClientReplay returns true
	// 从GameState的逻辑调用，用于在ShouldRecordClientReplay返回true时自动记录客户端的回放
	UFUNCTION(BlueprintCallable, Category = "Asce|PlayerController")
	UE_API bool TryToRecordClientReplay();

	// Call to see if we should record a replay, subclasses could change this
	// 检查是否记录回放，子类可重载
	UE_API virtual bool ShouldRecordClientReplay();

	// Run a cheat command on the server.
	// rpc, 在服务器上调用作弊命令
	UFUNCTION(Reliable, Server, WithValidation)
	UE_API void ServerCheat(const FString& Msg);

	// Run a cheat command on the server for all players.
	// 在服务器上为所有玩家调用作弊指令
	UFUNCTION(Reliable, Server, WithValidation)
	UE_API void ServerCheatAll(const FString& Msg);

	/** 客户端→服务器：低频上报视角焦点（本端Pawn位置；相机以固定偏移挂在Pawn上）。
	 *  服务器端这份观察者Pawn不参与玩法、只当MassLOD/MassReplication的viewer载体
	 *  （Config/DefaultMass.ini 里 bUsePlayerPawnLocationInsteadOfCamera=True 时 viewer 取Pawn位置；
	 *  远端玩家在服务器上没有相机更新，默认的GetPlayerViewPoint是死值）。
	 *  Unreliable：丢包下一拍覆盖即可，不需要重传。 */
	UFUNCTION(Unreliable, Server)
	UE_API void Server_UpdateViewFocus(const FVector_NetQuantize& Focus);

	//~AActor interface
	UE_API virtual void PreInitializeComponents() override;
	UE_API virtual void BeginPlay() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UE_API virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~End of AActor interface

	//~AController interface
	UE_API virtual void OnPossess(APawn* InPawn) override;
	UE_API virtual void OnUnPossess() override;
	UE_API virtual void InitPlayerState() override;
	UE_API virtual void CleanupPlayerState() override;
	UE_API virtual void OnRep_PlayerState() override;
	//~End of AController interface

	//~APlayerController interface
	UE_API virtual void ReceivedPlayer() override;
	UE_API virtual void PlayerTick(float DeltaTime) override;
	UE_API virtual void SetPlayer(UPlayer* InPlayer) override;
	UE_API virtual void AddCheats(bool bForce) override;
	UE_API virtual void UpdateForceFeedback(IInputInterface* InputInterface, const int32 ControllerId) override;
	UE_API virtual void UpdateHiddenComponents(const FVector& ViewLocation, TSet<FPrimitiveComponentId>& OutHiddenComponents) override;
	UE_API virtual void PreProcessInput(const float DeltaTime, const bool bGamePaused) override;
	UE_API virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;
	//~End of APlayerController interface

	//~ILyraCameraAssistInterface interface
	//UE_API virtual void OnCameraPenetratingTarget() override;
	//~End of ILyraCameraAssistInterface interface
	
	//~ILyraTeamAgentInterface interface
	UE_API virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	UE_API virtual FGenericTeamId GetGenericTeamId() const override;
	UE_API virtual FOnAsceTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() override;
	//~End of ILyraTeamAgentInterface interface

	UFUNCTION(BlueprintCallable, Category = "Asce|Character")
	UE_API void SetIsAutoRunning(const bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Asce|Character")
	UE_API bool GetIsAutoRunning() const;

private:
	UPROPERTY()
	FOnAsceTeamIndexChangedDelegate OnTeamChangedDelegate;

	UPROPERTY()
	TObjectPtr<APlayerState> LastSeenPlayerState;

private:
	UFUNCTION()
	void OnPlayerStateChangedTeam(UObject* TeamAgent, int32 OldTeam, int32 NewTeam);

protected:
	// Called when the player state is set or cleared
	UE_API virtual void OnPlayerStateChanged();

private:
	void BroadcastOnPlayerStateChanged();

protected:

	//~APlayerController interface

	//~End of APlayerController interface

	UE_API void OnSettingsChanged(UAsceSettingsShared* Settings);
	
	UE_API void OnStartAutoRun();
	UE_API void OnEndAutoRun();

	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="OnStartAutoRun"))
	UE_API void K2_OnStartAutoRun();

	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="OnEndAutoRun"))
	UE_API void K2_OnEndAutoRun();

	bool bHideViewTargetPawnNextFrame = false;
};


// A player controller used for replay capture and playback
UCLASS()
class AAsceReplayPlayerController : public AAscePlayerController
{
	GENERATED_BODY()

	virtual void Tick(float DeltaSeconds) override;
	virtual void SmoothTargetViewRotation(APawn* TargetPawn, float DeltaSeconds) override;
	virtual bool ShouldRecordClientReplay() override;

	// Callback for when the game state's RecorderPlayerState gets replicated during replay playback
	void RecorderPlayerStateUpdated(APlayerState* NewRecorderPlayerState);

	// Callback for when the followed player state changes pawn
	UFUNCTION()
	void OnPlayerStatePawnSet(APlayerState* ChangedPlayerState, APawn* NewPlayerPawn, APawn* OldPlayerPawn);

	// The player state we are currently following */
	UPROPERTY(Transient)
	TObjectPtr<APlayerState> FollowedPlayerState;
};

#undef UE_API
