// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagAssetInterface.h"
#include "ModularPawn.h"
#include "GameFramework/Pawn.h"
#include "Teams/AsceTeamAgentInterface.h"
#include "AsceRTSObserverPawn.generated.h"
#define UE_API ASCEGAME_API
class UAsceObserverComponent;
class UAsceOrderChannelComponent;
class AAscePlayerController;
class AAscePlayerState;
class UAscePawnExtensionComponent;
class UAsceCameraComponent;

//服务器创建并复制到客户端；Pawn的移动**不复制**，完全由客户端任意设置位置（相机权威在本地）。
//客户端以低频RPC把焦点上报给服务器（AAscePlayerController::Server_UpdateViewFocus），
//服务器那份Pawn仅作为MassLOD/MassReplication的viewer载体用（Config/DefaultMass.ini:
//bUsePlayerPawnLocationInsteadOfCamera=True），不参与玩法、也不会回传位置打断客户端。

UCLASS(MinimalAPI, Config = Game, Meta = (ShortTooltip = "The base rts observer pawn class used by this project."))
class AAsceRTSObserverPawn : public AModularPawn, public IAsceTeamAgentInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	UE_API AAsceRTSObserverPawn(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	
	UFUNCTION(BlueprintCallable, Category = "Asce|RTSObserver")
	UE_API AAscePlayerController* GetAscePlayerController() const;

	UFUNCTION(BlueprintCallable, Category = "Asce|RTSObserver")
	UE_API AAscePlayerState* GetAscePlayerState() const;
	
	//之后可能会加上ASC
	// UFUNCTION(BlueprintCallable, Category = "Asce|Character")
	// UE_API UAsceAbilitySystemComponent* GetAsceAbilitySystemComponent() const;
	// UE_API virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	// UE_API virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	// UE_API virtual bool HasMatchingGameplayTag(FGameplayTag TagToCheck) const override;
	// UE_API virtual bool HasAllMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const override;
	// UE_API virtual bool HasAnyMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const override;

	
	//~AActor interface
	UE_API virtual void PreInitializeComponents() override;
	UE_API virtual void BeginPlay() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UE_API virtual void Reset() override;
	UE_API virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UE_API virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
	//~End of AActor interface
	
	
	//~APawn interface
	UE_API virtual void NotifyControllerChanged() override;
	//~End of APawn interface

	//~IAsceTeamAgentInterface interface
	UE_API virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	UE_API virtual FGenericTeamId GetGenericTeamId() const override;
	UE_API virtual FOnAsceTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() override;
	//~End of IAsceTeamAgentInterface interface

	
protected:

	// UE_API virtual void OnAbilitySystemInitialized();
	// UE_API virtual void OnAbilitySystemUninitialized();

	UE_API virtual void PossessedBy(AController* NewController) override;
	UE_API virtual void UnPossessed() override;

	UE_API virtual void OnRep_Controller() override;

	// 本地玩家的鼠标/输入模式设置。
	// 注意 UE5.7 起客户端 possession 不调用 PossessedBy（客户端走 ClientRestart->Restart/OnRep_Controller），
	// 所以"服务器本地玩家"（听服 host）由 PossessedBy 调用，"客户端本地玩家"由 OnRep_Controller 调用。
	UE_API void ApplyLocalPlayerInputSetup(APlayerController* PlayerController);
	UE_API virtual void OnRep_PlayerState() override;

	UE_API virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// UE_API void InitializeGameplayTags();

	UE_API virtual void FellOutOfWorld(const class UDamageType& dmgType) override;
	
	UE_API void UninitAndDestroy();
	
	// Called to determine what happens to the team ID when possession ends
	virtual FGenericTeamId DetermineNewTeamAfterPossessionEnds(FGenericTeamId OldTeamID) const
	{
		// This could be changed to return, e.g., OldTeamID if you want to keep it assigned afterwards, or return an ID for some neutral faction, or etc...
		return FGenericTeamId::NoTeam;
	}

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asce|Character", Meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAscePawnExtensionComponent> PawnExtComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asce|Character", Meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAsceCameraComponent> CameraComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asce|Character", Meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAsceObserverComponent> ObserverComponent;

	/** 指令通道（Server RPC + 客户端本地预测）；没有它时 Commander 会退化成本地直写（只有本端能看到单位移动）。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asce|Character", Meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAsceOrderChannelComponent> OrderChannelComponent;
	
	UPROPERTY(ReplicatedUsing = OnRep_MyTeamID)
	FGenericTeamId MyTeamID;
	
	UPROPERTY()
	FOnAsceTeamIndexChangedDelegate OnTeamChangedDelegate;
	
	UFUNCTION()
	UE_API void OnControllerChangedTeam(UObject* TeamAgent, int32 OldTeam, int32 NewTeam);
	
	UFUNCTION()
	UE_API void OnRep_MyTeamID(FGenericTeamId OldTeamID);
};
#undef UE_API
