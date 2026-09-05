// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayAbilitySpec.h"
#include "Abilities/GameplayAbility.h"
#include "AsceAbilityCost.generated.h"

class UAsceGameplayAbility;

/**
 * UAsceAbilityCost
 *
 * Base class for costs that a AsceGameplayAbility has (e.g., ammo or charges)
 */

// DefaultToInstanced将保证此类被在其他对象中的实例总是被看作instanced的
// EditInlineNew将保证此类在其他对象中被创建时，始终实例化（即使在声明的指针中没有标识instanced？）
// 二者共同作用使得默认情况下在其他类中添加此类的实例总是会实例化
UCLASS(MinimalAPI, DefaultToInstanced, EditInlineNew, Abstract)
class UAsceAbilityCost : public UObject
{
	GENERATED_BODY()

public:
	UAsceAbilityCost()
	{
	}

	/**
	 * Checks if we can afford this cost.
	 *
	 * A failure reason tag can be added to OptionalRelevantTags (if non-null), which can be queried
	 * elsewhere to determine how to provide user feedback (e.g., a clicking noise if a weapon is out of ammo)
	 * 
	 * Ability and ActorInfo are guaranteed to be non-null on entry, but OptionalRelevantTags can be nullptr.
	 * 
	 * @return true if we can pay for the ability, false otherwise.
	 * 
	 * 检查是否能支付激活能力的代价
	 * 失败原因的tag将会被添加到可选相关tags容器中（如果传入的容器指针不为null）， 随后可以在此容器中查询失败原因来决定如何提供反馈
	 * Ability和ActorInfo必须不为空，但OptionalRelevantTags可以为空
	 *
	 */
	virtual bool CheckCost(const UAsceGameplayAbility* Ability, const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
	{
		return true;
	}

	/**
	 * Applies the ability's cost to the target
	 *
	 * Notes:
	 * - Your implementation don't need to check ShouldOnlyApplyCostOnHit(), the caller does that for you.
	 * - Ability and ActorInfo are guaranteed to be non-null on entry.
	 * 
	 * 对激活能力的目标应用能力的代价
	 * 继承此函数后，不需要实际应用前调用checkcost来检查是否满足条件，此函数的调用者应该首先检查并且在满足时才调用此函数
	 * 同样Ability和ActorInfo不能为空
	 */
	virtual void ApplyCost(const UAsceGameplayAbility* Ability, const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
	{
	}

	/** If true, this cost should only be applied if this ability hits successfully */
	// 此值为true，代价仅在能力成功命中时才需要支付
	bool ShouldOnlyApplyCostOnHit() const { return bOnlyApplyCostOnHit; }

protected:
	/** If true, this cost should only be applied if this ability hits successfully */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Costs)
	bool bOnlyApplyCostOnHit = false;
};
