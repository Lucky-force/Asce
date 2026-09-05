#pragma once

#include "AbilitySystemComponent.h"
#include "AsceAttributeSet.h"
#include "NativeGameplayTags.h"

#include "AsceHealthSet.generated.h"

#define UE_API ASCEGAME_API

class UObject;
struct FFrame;

ASCEGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Gameplay_Damage);
ASCEGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Gameplay_DamageImmunity);
ASCEGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Gameplay_DamageSelfDestruct);
ASCEGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Gameplay_FellOutOfWorld);
ASCEGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Asce_Damage_Message);

struct FGameplayEffectModCallbackData;


/**
 * UAsceHealthSet
 *
 *	Class that defines attributes that are necessary for taking damage.
 *	Attribute examples include: health, shields, and resistances.
 */
UCLASS(MinimalAPI, BlueprintType)
class UAsceHealthSet : public UAsceAttributeSet
{
	GENERATED_BODY()

public:

	UE_API UAsceHealthSet();

	ATTRIBUTE_ACCESSORS(UAsceHealthSet, Health);
	ATTRIBUTE_ACCESSORS(UAsceHealthSet, MaxHealth);
	ATTRIBUTE_ACCESSORS(UAsceHealthSet, Healing);
	ATTRIBUTE_ACCESSORS(UAsceHealthSet, Damage);

	// Delegate when health changes due to damage/healing, some information may be missing on the client
	// 由于伤害或者治疗而改变生命值的时候广播，一些信息可能会在客户端丢失
	mutable FAsceAttributeEvent OnHealthChanged;

	// Delegate when max health changes
	// 最大生命值改变的时候广播
	mutable FAsceAttributeEvent OnMaxHealthChanged;

	// Delegate to broadcast when the health attribute reaches zero
	// 生命值归零的时候广播
	mutable FAsceAttributeEvent OnOutOfHealth;

protected:

	UFUNCTION()
	UE_API void OnRep_Health(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	UE_API void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

	UE_API virtual bool PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data) override;
	UE_API virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	UE_API virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	UE_API virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	UE_API virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;

	UE_API void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;

private:

	// The current health attribute.  The health will be capped by the max health attribute.  Health is hidden from modifiers so only executions can modify it.
	// 当前生命值属性，生命值会被最大生命值限制。并且生命值标记了HideFromModifiers元数据，因此只有Executions可以修改它
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health, Category = "Asce|Health", Meta = (HideFromModifiers, AllowPrivateAccess = true))
	FGameplayAttributeData Health;

	// The current max health attribute.  Max health is an attribute since gameplay effects can modify it.
	// 当前最大生命值，没有标记HideFromModifiers，因此GE就能修改它
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth, Category = "Asce|Health", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData MaxHealth;

	// Used to track when the health reaches 0.
	// 用来追踪生命值是否为0
	bool bOutOfHealth;

	// Store the health before any changes 
	// 存放修改之前的值（oldvalue是在修改的时候有效的，将其存下来）
	float MaxHealthBeforeAttributeChange;
	float HealthBeforeAttributeChange;

	// -------------------------------------------------------------------
	//	Meta Attribute (please keep attributes that aren't 'stateful' below
	// 元属性，下面这些属性是非状态的
	// -------------------------------------------------------------------

	// Incoming healing. This is mapped directly to +Health
	UPROPERTY(BlueprintReadOnly, Category="Asce|Health", Meta=(AllowPrivateAccess=true))
	FGameplayAttributeData Healing;

	// Incoming damage. This is mapped directly to -Health
	UPROPERTY(BlueprintReadOnly, Category="Asce|Health", Meta=(HideFromModifiers, AllowPrivateAccess=true))
	FGameplayAttributeData Damage;
};

#undef UE_API
