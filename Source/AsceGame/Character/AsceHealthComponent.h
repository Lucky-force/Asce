#pragma once

#include "Components/GameFrameworkComponent.h"

#include "AsceHealthComponent.generated.h"

#define UE_API ASCEGAME_API

class UAsceHealthComponent;

class UAsceAbilitySystemComponent;
class UAsceHealthSet;
class UObject;
struct FFrame;
struct FGameplayEffectSpec;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAsceHealth_DeathEvent, AActor*, OwningActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FAsceHealth_AttributeChanged, UAsceHealthComponent*, HealthComponent, float, OldValue, float, NewValue, AActor*, Instigator);

/**
 * EAsceDeathState
 *
 *	Defines current state of death.
 */
UENUM(BlueprintType)
enum class EAsceDeathState : uint8
{
	NotDead = 0,
	DeathStarted,
	DeathFinished
};


/**
 * UAsceHealthComponent
 *
 *	An actor component used to handle anything related to health.
 */
UCLASS(MinimalAPI, Blueprintable, Meta=(BlueprintSpawnableComponent))
class UAsceHealthComponent : public UGameFrameworkComponent
{
	GENERATED_BODY()

public:

	UE_API UAsceHealthComponent(const FObjectInitializer& ObjectInitializer);

	// Returns the health component if one exists on the specified actor.
	UFUNCTION(BlueprintPure, Category = "Asce|Health")
	static UAsceHealthComponent* FindHealthComponent(const AActor* Actor) { return (Actor ? Actor->FindComponentByClass<UAsceHealthComponent>() : nullptr); }

	// Initialize the component using an ability system component.
	// 使用ASC初始化
	UFUNCTION(BlueprintCallable, Category = "Asce|Health")
	UE_API void InitializeWithAbilitySystem(UAsceAbilitySystemComponent* InASC);

	// Uninitialize the component, clearing any references to the ability system.
	// 反初始化，清理对ASC的引用
	UFUNCTION(BlueprintCallable, Category = "Asce|Health")
	UE_API void UninitializeFromAbilitySystem();

	// Returns the current health value.
	UFUNCTION(BlueprintCallable, Category = "Asce|Health")
	UE_API float GetHealth() const;

	// Returns the current maximum health value.
	UFUNCTION(BlueprintCallable, Category = "Asce|Health")
	UE_API float GetMaxHealth() const;

	// Returns the current health in the range [0.0, 1.0].
	// 归一化生命值
	UFUNCTION(BlueprintCallable, Category = "Asce|Health")
	UE_API float GetHealthNormalized() const;

	UFUNCTION(BlueprintCallable, Category = "Asce|Health")
	EAsceDeathState GetDeathState() const { return DeathState; }

	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Asce|Health", Meta = (ExpandBoolAsExecs = "ReturnValue"))
	bool IsDeadOrDying() const { return (DeathState > EAsceDeathState::NotDead); }

	// Begins the death sequence for the owner.
	// 开始死亡sequnce（开始死亡）
	UE_API virtual void StartDeath();

	// Ends the death sequence for the owner.
	// 结束死亡sequence（真正死亡）
	UE_API virtual void FinishDeath();

	// Applies enough damage to kill the owner.
	// 自杀
	UE_API virtual void DamageSelfDestruct(bool bFellOutOfWorld = false);

public:

	// Delegate fired when the health value has changed. This is called on the client but the instigator may not be valid
	UPROPERTY(BlueprintAssignable)
	FAsceHealth_AttributeChanged OnHealthChanged;

	// Delegate fired when the max health value has changed. This is called on the client but the instigator may not be valid
	// 最大生命值修改时回调，在客户端调用，instigator可能是非法的
	UPROPERTY(BlueprintAssignable)
	FAsceHealth_AttributeChanged OnMaxHealthChanged;

	// Delegate fired when the death sequence has started.
	UPROPERTY(BlueprintAssignable)
	FAsceHealth_DeathEvent OnDeathStarted;

	// Delegate fired when the death sequence has finished.
	UPROPERTY(BlueprintAssignable)
	FAsceHealth_DeathEvent OnDeathFinished;

protected:

	UE_API virtual void OnUnregister() override;

	UE_API void ClearGameplayTags();

	UE_API virtual void HandleHealthChanged(AActor* DamageInstigator, AActor* DamageCauser, const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue);
	UE_API virtual void HandleMaxHealthChanged(AActor* DamageInstigator, AActor* DamageCauser, const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue);
	UE_API virtual void HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser, const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue);

	UFUNCTION()
	UE_API virtual void OnRep_DeathState(EAsceDeathState OldDeathState);

protected:

	// Ability system used by this component.
	UPROPERTY()
	TObjectPtr<UAsceAbilitySystemComponent> AbilitySystemComponent;

	// Health set used by this component.
	UPROPERTY()
	TObjectPtr<const UAsceHealthSet> HealthSet;

	// Replicated state used to handle dying.
	// 服务器复制过来的值，用来处理死亡逻辑
	UPROPERTY(ReplicatedUsing = OnRep_DeathState)
	EAsceDeathState DeathState;
};

#undef UE_API
