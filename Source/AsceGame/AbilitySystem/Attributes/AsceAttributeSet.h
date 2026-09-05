#pragma once

#include "AttributeSet.h"

#include "AsceAttributeSet.generated.h"

#define UE_API ASCEGAME_API

class AActor;
class UAsceAbilitySystemComponent;
class UObject;
class UWorld;
struct FGameplayEffectSpec;


/**
 * This macro defines a set of helper functions for accessing and initializing attributes.
 *
 * The following example of the macro:
 *		ATTRIBUTE_ACCESSORS(UAsceHealthSet, Health)
 * will create the following functions:
 *		static FGameplayAttribute GetHealthAttribute();
 *		float GetHealth() const;
 *		void SetHealth(float NewVal);
 *		void InitHealth(float NewVal);
 */
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/** 
 * Delegate used to broadcast attribute events, some of these parameters may be null on clients: 
 * 用来广播属性变化的委托，一些参数在客户端可能是null
 * @param EffectInstigator	The original instigating actor for this event，这个事件的Instigator（比如伤害的造成者Instigator）
 * @param EffectCauser		The physical actor that caused the change，这个事件的物理造成者（比如由Instigator发射的子弹）
 * @param EffectSpec		The full effect spec for this change，造成这个事件的effectspec（用于在Target的ASC上查询Effect）
 * @param EffectMagnitude	The raw magnitude, this is before clamping，系数
 * @param OldValue			The value of the attribute before it was changed，旧值
 * @param NewValue			The value after it was changed，新值
*/
DECLARE_MULTICAST_DELEGATE_SixParams(FAsceAttributeEvent, AActor* /*EffectInstigator*/, AActor* /*EffectCauser*/, const FGameplayEffectSpec* /*EffectSpec*/, float /*EffectMagnitude*/, float /*OldValue*/, float /*NewValue*/);

/**
 * UAsceAttributeSet
 *
 *	Base attribute set class for the project.
 */
UCLASS(MinimalAPI)
class UAsceAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:

	UE_API UAsceAttributeSet();

	UE_API UWorld* GetWorld() const override;

	UE_API UAsceAbilitySystemComponent* GetAsceAbilitySystemComponent() const;
};

#undef UE_API
