// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Abilities/GameplayAbility.h"

#include "AsceGameplayAbility.generated.h"

#define UE_API ASCEGAME_API

struct FGameplayAbilityActivationInfo;
struct FGameplayAbilitySpec;
struct FGameplayAbilitySpecHandle;

class AActor;
class AController;
class AAsceCharacter;
class AAscePlayerController;
class APlayerController;
class FText;
class IAsceAbilitySourceInterface;
class UAnimMontage;
class UAsceAbilityCost;
class UAsceAbilitySystemComponent;
class UAsceCameraMode;
class UAsceHeroComponent;
class UObject;
struct FFrame;
struct FGameplayAbilityActorInfo;
struct FGameplayEffectSpec;
struct FGameplayEventData;

/**
 * EAsceAbilityActivationPolicy
 *
 *	Defines how an ability is meant to activate.
 *	定义能力如何激活
 */
UENUM(BlueprintType)
enum class EAsceAbilityActivationPolicy : uint8
{
	// Try to activate the ability when the input is triggered.
	// 仅在输入首次触发尝试时激活
	OnInputTriggered,
	
	// Continually try to activate the ability while the input is active.
	// 只要输入有效，就会一直尝试激活
	WhileInputActive,

	// Try to activate the ability when an avatar is assigned.
	// avatar被赋予玩家时尝试激活此能力
	OnSpawn
};


/**
 * EAsceAbilityActivationGroup
 *
 *	Defines how an ability activates in relation to other abilities.
 *	此枚举定义能力激活时和其他能力的联系
 */
// UENUM(BlueprintType)
// enum class EAsceAbilityActivationGroup : uint8
// {
// 	// Ability runs independently of all other abilities.
// 	// 独立能力，不与其他能力相关联
// 	Independent,
//
// 	// Ability is canceled and replaced by other exclusive abilities.
// 	// 此能力会被其他同类型的仅能被单独作用的能力(独特能力)取消并取代
// 	Exclusive_Replaceable,
//
// 	// Ability blocks all other exclusive abilities from activating.
// 	// 此能力会阻塞所有其他的独特能力被激活
// 	Exclusive_Blocking,
//
// 	MAX	UMETA(Hidden)
// };

/** Failure reason that can be used to play an animation montage when a failure occurs */
// 当激活能力失败时，会返回此信息，可以用来播放一段蒙太奇来表现能力触发失败
USTRUCT(BlueprintType)
struct FAsceAbilityMontageFailureMessage
{
	GENERATED_BODY()

public:
	// Player controller that failed to activate the ability, if the AbilitySystemComponent was player owned
	// 如果AbilitySystemComponent由玩家角色拥有，此值将有效并代表此玩家的controller
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<APlayerController> PlayerController = nullptr;

	// Avatar actor that failed to activate the ability
	// 激活能力失败的avatar
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> AvatarActor = nullptr;

	// All the reasons why this ability has failed
	// 能力激活失败的原因
	UPROPERTY(BlueprintReadWrite)
	FGameplayTagContainer FailureTags;

	// 能力激活失败要播放的蒙太奇
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UAnimMontage> FailureMontage = nullptr;
};

/**
 * UAsceGameplayAbility
 *
 *	The base gameplay ability class used by this project.
 */
UCLASS(MinimalAPI, Abstract, HideCategories = Input, Meta = (ShortTooltip = "The base gameplay ability class used by this project."))
class UAsceGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()
	friend class UAsceAbilitySystemComponent;

public:

	UE_API UAsceGameplayAbility(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, Category = "Asce|Ability")
	UE_API UAsceAbilitySystemComponent* GetAsceAbilitySystemComponentFromActorInfo() const;

	UFUNCTION(BlueprintCallable, Category = "Asce|Ability")
	UE_API AAscePlayerController* GetAscePlayerControllerFromActorInfo() const;

	UFUNCTION(BlueprintCallable, Category = "Asce|Ability")
	UE_API AController* GetControllerFromActorInfo() const;

	UFUNCTION(BlueprintCallable, Category = "Asce|Ability")
	UE_API AAsceCharacter* GetAsceCharacterFromActorInfo() const;

	UFUNCTION(BlueprintCallable, Category = "Asce|Ability")
	UE_API UAsceHeroComponent* GetHeroComponentFromActorInfo() const;

	EAsceAbilityActivationPolicy GetActivationPolicy() const { return ActivationPolicy; }
	//EAsceAbilityActivationGroup GetActivationGroup() const { return ActivationGroup; }

	UE_API void TryActivateAbilityOnSpawn(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) const;

	// Returns true if the requested activation group is a valid transition.
	// 如果请求的激活组能合法转变则返回true
	//UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Asce|Ability", Meta = (ExpandBoolAsExecs = "ReturnValue"))
	//UE_API bool CanChangeActivationGroup(EAsceAbilityActivationGroup NewGroup) const;

	// Tries to change the activation group.  Returns true if it successfully changed.
	// 尝试转变激活组， 如果成功，则返回true
	// UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Asce|Ability", Meta = (ExpandBoolAsExecs = "ReturnValue"))
	// UE_API bool ChangeActivationGroup(EAsceAbilityActivationGroup NewGroup);

	// Sets the ability's camera mode.
	// 设置该能力的相机模式
	UFUNCTION(BlueprintCallable, Category = "Asce|Ability")
	UE_API void SetCameraMode(TSubclassOf<UAsceCameraMode> CameraMode);

	// Clears the ability's camera mode.  Automatically called if needed when the ability ends.
	// 清除该能力的相机模式，如果需要，则会在能力结束的时候自动调用
	UFUNCTION(BlueprintCallable, Category = "Asce|Ability")
	UE_API void ClearCameraMode();

	// 能力激活失败时的回调, 分别传给脚本层和c++层
	void OnAbilityFailedToActivate(const FGameplayTagContainer& FailedReason) const
	{
		NativeOnAbilityFailedToActivate(FailedReason);
		ScriptOnAbilityFailedToActivate(FailedReason);
	}

protected:

	// Called when the ability fails to activate
	UE_API virtual void NativeOnAbilityFailedToActivate(const FGameplayTagContainer& FailedReason) const;

	// Called when the ability fails to activate
	UFUNCTION(BlueprintImplementableEvent)
	UE_API void ScriptOnAbilityFailedToActivate(const FGameplayTagContainer& FailedReason) const;

	//~UGameplayAbility interface
	UE_API virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const override;
	UE_API virtual void SetCanBeCanceled(bool bCanBeCanceled) override;
	UE_API virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	UE_API virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	UE_API virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	UE_API virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	UE_API virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	UE_API virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
	UE_API virtual FGameplayEffectContextHandle MakeEffectContext(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const override;
	UE_API virtual void ApplyAbilityTagsToGameplayEffectSpec(FGameplayEffectSpec& Spec, FGameplayAbilitySpec* AbilitySpec) const override;
	UE_API virtual bool DoesAbilitySatisfyTagRequirements(const UAbilitySystemComponent& AbilitySystemComponent, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	//~End of UGameplayAbility interface

	UE_API virtual void OnPawnAvatarSet();

	UE_API virtual void GetAbilitySource(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, float& OutSourceLevel, const IAsceAbilitySourceInterface*& OutAbilitySource, AActor*& OutEffectCauser) const;

	/** Called when this ability is granted to the ability system component. */
	UFUNCTION(BlueprintImplementableEvent, Category = Ability, DisplayName = "OnAbilityAdded")
	UE_API void K2_OnAbilityAdded();

	/** Called when this ability is removed from the ability system component. */
	UFUNCTION(BlueprintImplementableEvent, Category = Ability, DisplayName = "OnAbilityRemoved")
	UE_API void K2_OnAbilityRemoved();

	/** Called when the ability system is initialized with a pawn avatar. */
	UFUNCTION(BlueprintImplementableEvent, Category = Ability, DisplayName = "OnPawnAvatarSet")
	UE_API void K2_OnPawnAvatarSet();

protected:

	// Defines how this ability is meant to activate.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asce|Ability Activation")
	EAsceAbilityActivationPolicy ActivationPolicy;

	// Defines the relationship between this ability activating and other abilities activating.
	// UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asce|Ability Activation")
	// EAsceAbilityActivationGroup ActivationGroup;

	// Additional costs that must be paid to activate this ability
	UPROPERTY(EditDefaultsOnly, Instanced, Category = Costs)
	TArray<TObjectPtr<UAsceAbilityCost>> AdditionalCosts;

	// Map of failure tags to simple error messages
	// 存放各种失败原因下的失败信息
	UPROPERTY(EditDefaultsOnly, Category = "Advanced")
	TMap<FGameplayTag, FText> FailureTagToUserFacingMessages;

	// Map of failure tags to anim montages that should be played with them
	// 存放各种失败原因下可以播放的蒙太奇
	UPROPERTY(EditDefaultsOnly, Category = "Advanced")
	TMap<FGameplayTag, TObjectPtr<UAnimMontage>> FailureTagToAnimMontage;

	// If true, extra information should be logged when this ability is canceled. This is temporary, used for tracking a bug.
	// 设置为true，当能力取消的时候会打印额外的信息，临时使用对追踪bug很有效
	UPROPERTY(EditDefaultsOnly, Category = "Advanced")
	bool bLogCancelation;

	// Current camera mode set by the ability.
	// 此能力会设置的相机模式
	TSubclassOf<UAsceCameraMode> ActiveCameraMode;
};

#undef UE_API
