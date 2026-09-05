// Fill out your copyright notice in the Description page of Project Settings.
#include "AngelScriptGameFrameworkComponentMixin.h"
#include "Components/GameFrameworkInitStateInterface.h"
#include "Components/PawnComponent.h"




void UAngelScriptGameFrameworkComponentMixin::RegisterAndCallForInitStateChange(UGameFrameworkComponent* Component, FGameplayTag RequiredState, FActorInitStateChangedBPDelegate Delegate, bool bCallImmediately)
{
	if (IGameFrameworkInitStateInterface* Interface = Cast<IGameFrameworkInitStateInterface>(Component))
	{
		Interface->RegisterAndCallForInitStateChange(RequiredState, Delegate, bCallImmediately);
	}
}

void UAngelScriptGameFrameworkComponentMixin::UnregisterInitStateDelegate(UGameFrameworkComponent* Component, FActorInitStateChangedBPDelegate Delegate)
{
	if (IGameFrameworkInitStateInterface* Interface = Cast<IGameFrameworkInitStateInterface>(Component))
	{
		Interface->UnregisterInitStateDelegate(Delegate);
	}
}
