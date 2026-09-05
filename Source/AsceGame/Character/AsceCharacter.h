// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AbilitySystemInterface.h"
#include "GameplayCueInterface.h"
#include "GameplayTagAssetInterface.h"
#include "ModularCharacter.h"
#include "FeedBack/ContextEffects/AsceContextEffectsInterface.h"
#include "Teams/AsceTeamAgentInterface.h"

#include "AsceCharacter.generated.h"

#define UE_API ASCEGAME_API

class AActor;
class AController;
class AAscePlayerController;
class AAscePlayerState;
class FLifetimeProperty;
class IRepChangedPropertyTracker;
class UAbilitySystemComponent;
class UInputComponent;
class UAsceAbilitySystemComponent;
class UAsceCameraComponent;
class UAsceHealthComponent;
class UAscePawnExtensionComponent;
class UObject;
struct FFrame;
struct FGameplayTag;
struct FGameplayTagContainer;


/**
 * FAsceReplicatedAcceleration: Compressed representation of acceleration
 */
USTRUCT()
struct FAsceReplicatedAcceleration
{
	//计算方式在AAsceCharacter的PreReplication中
	GENERATED_BODY()

	UPROPERTY()
	uint8 AccelXYRadians = 0;	// Direction of XY accel component, quantized to represent [0, 2*pi]
	//XY平面上的加速度方向，转化为0~2*PI再进行计算

	UPROPERTY()
	uint8 AccelXYMagnitude = 0;	//Accel rate of XY component, quantized to represent [0, MaxAcceleration]
	//XY平面上的加速度速率, 0~256
	
	UPROPERTY()
	int8 AccelZ = 0;	// Raw Z accel rate component, quantized to represent [-MaxAcceleration, MaxAcceleration]
	//Z轴加速度，-128~128
};

/** The type we use to send FastShared movement updates. */
//用来快速同步移动
USTRUCT()
struct FSharedRepMovement
{
	GENERATED_BODY()

	FSharedRepMovement();

	bool FillForCharacter(ACharacter* Character);
	bool Equals(const FSharedRepMovement& Other, ACharacter* Character) const;

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

	UPROPERTY(Transient)
	FRepMovement RepMovement;//移动信息，包含了加速度，角速度，位置，角度，服务器当前帧数等数据

	UPROPERTY(Transient)
	float RepTimeStamp = 0.0f;//同步时间戳

	UPROPERTY(Transient)
	uint8 RepMovementMode = 0;//移动模式

	UPROPERTY(Transient)
	bool bProxyIsJumpForceApplied = false;//强制让客户端设置为正在跳跃

	UPROPERTY(Transient)
	bool bIsCrouched = false;//是否在下蹲状态
};

template<>
struct TStructOpsTypeTraits<FSharedRepMovement> : public TStructOpsTypeTraitsBase2<FSharedRepMovement>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
	};
};

/**
 * AAsceCharacter
 *
 *	The base character pawn class used by this project.
 *	Responsible for sending events to pawn components.
 *	New behavior should be added via pawn components when possible.
 *	基础角色Pawn，可以响应发送到PawnComponents中的事件，新的行为和能力应该通过添加PawnComponents来实现
 */
UCLASS(MinimalAPI, Config = Game, Meta = (ShortTooltip = "The base character pawn class used by this project."))
class AAsceCharacter : public AModularCharacter, public IAbilitySystemInterface, public IGameplayCueInterface, public IGameplayTagAssetInterface, public IAsceTeamAgentInterface, public IAsceContextEffectsInterface
{
	GENERATED_BODY()

public:

	UE_API AAsceCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, Category = "Asce|Character")
	UE_API AAscePlayerController* GetAscePlayerController() const;

	UFUNCTION(BlueprintCallable, Category = "Asce|Character")
	UE_API AAscePlayerState* GetAscePlayerState() const;

	UFUNCTION(BlueprintCallable, Category = "Asce|Character")
	UE_API UAsceAbilitySystemComponent* GetAsceAbilitySystemComponent() const;
	UE_API virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UE_API virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	UE_API virtual bool HasMatchingGameplayTag(FGameplayTag TagToCheck) const override;
	UE_API virtual bool HasAllMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const override;
	UE_API virtual bool HasAnyMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const override;

	UE_API void ToggleCrouch();//触发下蹲

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
	
	/** RPCs that is called on frames when default property replication is skipped. This replicates a single movement update to everyone. */
	//当默认属性复制被跳过时，每一帧应该调用此函数来广播所有角色的移动状态到客户端
	UFUNCTION(NetMulticast, unreliable)
	UE_API void FastSharedReplication(const FSharedRepMovement& SharedRepMovement);

	// Last FSharedRepMovement we sent, to avoid sending repeatedly.
	// 记录上一次发送的移动状态，防止玩家不移动了还在一直重复发送相同的移动数据
	FSharedRepMovement LastSharedReplication;

	UE_API virtual bool UpdateSharedReplication();

protected:

	UE_API virtual void OnAbilitySystemInitialized();
	UE_API virtual void OnAbilitySystemUninitialized();

	UE_API virtual void PossessedBy(AController* NewController) override;
	UE_API virtual void UnPossessed() override;

	UE_API virtual void OnRep_Controller() override;
	UE_API virtual void OnRep_PlayerState() override;

	UE_API virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UE_API void InitializeGameplayTags();

	UE_API virtual void FellOutOfWorld(const class UDamageType& dmgType) override;

	// Begins the death sequence for the character (disables collision, disables movement, etc...)
	// 开启死亡sequence，关闭碰撞，关闭移动等等死亡时要做的事情
	UFUNCTION()
	UE_API virtual void OnDeathStarted(AActor* OwningActor);

	// Ends the death sequence for the character (detaches controller, destroys pawn, etc...)
	// 死亡sequence播完了，从controller上面detache，销毁pawn（或许应该放到对象池中）
	UFUNCTION()
	UE_API virtual void OnDeathFinished(AActor* OwningActor);

	UE_API void DisableMovementAndCollision();
	UE_API void DestroyDueToDeath();
	UE_API void UninitAndDestroy();

	// Called when the death sequence for the character has completed
	// 死亡sequence播完时回调给蓝图
	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="OnDeathFinished"))
	UE_API void K2_OnDeathFinished();

	
	//下面这些函数基本上就是把移动状态在ASC上用Tag再记录一遍
	//注意，不需要使用复制的LooseTag，因为这些移动状态本身就会复制，复制时自然会在客户端调用这些函数
	//此时就会直接在客户端更新LooseTag
	UE_API virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;
	UE_API void SetMovementModeTag(EMovementMode MovementMode, uint8 CustomMovementMode, bool bTagEnabled);

	UE_API virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	UE_API virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	UE_API virtual bool CanJumpInternal_Implementation() const;

private:

	//附加的组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asce|Character", Meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAscePawnExtensionComponent> PawnExtComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asce|Character", Meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAsceHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asce|Character", Meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAsceCameraComponent> CameraComponent;

	//存放加速度信息，由服务器复制, 只会复制到模拟代理，而主控代理将使用上面的完整数据
	UPROPERTY(Transient, ReplicatedUsing = OnRep_ReplicatedAcceleration)
	FAsceReplicatedAcceleration ReplicatedAcceleration;

	UPROPERTY(ReplicatedUsing = OnRep_MyTeamID)
	FGenericTeamId MyTeamID;

	UPROPERTY()
	FOnAsceTeamIndexChangedDelegate OnTeamChangedDelegate;

protected:
	// Called to determine what happens to the team ID when possession ends
	virtual FGenericTeamId DetermineNewTeamAfterPossessionEnds(FGenericTeamId OldTeamID) const
	{
		// This could be changed to return, e.g., OldTeamID if you want to keep it assigned afterwards, or return an ID for some neutral faction, or etc...
		return FGenericTeamId::NoTeam;
	}

private:
	UFUNCTION()
	UE_API void OnControllerChangedTeam(UObject* TeamAgent, int32 OldTeam, int32 NewTeam);

	UFUNCTION()
	UE_API void OnRep_ReplicatedAcceleration();

	UFUNCTION()
	UE_API void OnRep_MyTeamID(FGenericTeamId OldTeamID);
};

#undef UE_API
