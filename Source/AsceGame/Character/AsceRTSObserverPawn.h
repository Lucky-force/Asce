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
class AAscePlayerController;
class AAscePlayerState;
class UAscePawnExtensionComponent;
class UAsceCameraComponent;

//服务器创建并复制到客户端，随后不进行同步，完全由客户端任意的设置位置

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