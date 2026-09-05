#include "AsceAttributeSet.h"

#include "AbilitySystem/AsceAbilitySystemComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceAttributeSet)

class UWorld;


UAsceAttributeSet::UAsceAttributeSet()
{
}

UWorld* UAsceAttributeSet::GetWorld() const
{
	const UObject* Outer = GetOuter();
	check(Outer);

	return Outer->GetWorld();
}

UAsceAbilitySystemComponent* UAsceAttributeSet::GetAsceAbilitySystemComponent() const
{
	return Cast<UAsceAbilitySystemComponent>(GetOwningAbilitySystemComponent());
}

