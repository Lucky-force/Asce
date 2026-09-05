#pragma once

#include "AbilitySystemComponent.h"
#include "AsceAttributeSet.h"

#include "AsceCombatSet.generated.h"

class UObject;
struct FFrame;


/**
 * UAsceCombatSet
 *
 *  Class that defines attributes that are necessary for applying damage or healing.
 *	Attribute examples include: damage, healing, attack power, and shield penetrations.
 */
UCLASS(BlueprintType)
class UAsceCombatSet : public UAsceAttributeSet
{
	GENERATED_BODY()

public:

	UAsceCombatSet();

	ATTRIBUTE_ACCESSORS(UAsceCombatSet, BaseDamage);
	ATTRIBUTE_ACCESSORS(UAsceCombatSet, BaseHeal);

protected:

	UFUNCTION()
	void OnRep_BaseDamage(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_BaseHeal(const FGameplayAttributeData& OldValue);

private:

	// The base amount of damage to apply in the damage execution.
	// 基础伤害
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_BaseDamage, Category = "Asce|Combat", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData BaseDamage;

	// The base amount of healing to apply in the heal execution.
	// 基础治疗
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_BaseHeal, Category = "Asce|Combat", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData BaseHeal;
};
