// Fill out your copyright notice in the Description page of Project Settings.


#include "AsceAbilitySystemGlobals.h"

#include "AsceGameplayEffectContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceAbilitySystemGlobals)

struct FGameplayEffectContext;

UAsceAbilitySystemGlobals::UAsceAbilitySystemGlobals(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FGameplayEffectContext* UAsceAbilitySystemGlobals::AllocGameplayEffectContext() const
{
	return new FAsceGameplayEffectContext();
}

