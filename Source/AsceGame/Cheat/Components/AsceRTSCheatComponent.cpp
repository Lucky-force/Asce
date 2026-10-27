// Fill out your copyright notice in the Description page of Project Settings.

#include "Cheat/Components/AsceRTSCheatComponent.h"

#include "Cheat/Actions/RTS/AsceRTSCheatLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRTSCheatComponent)

bool UAsceRTSCheatComponent::ExecuteLine(const FString& Line, FString& OutSummary, FString& OutError)
{
	return UAsceRTSCheatLibrary::RunFromLine(this, GetOwnerPlayerController(), Line, OutSummary, OutError);
}
