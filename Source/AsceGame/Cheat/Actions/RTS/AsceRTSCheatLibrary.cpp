// Fill out your copyright notice in the Description page of Project Settings.

#include "Cheat/Actions/RTS/AsceRTSCheatLibrary.h"

#include "Cheat/Actions/RTS/AsceRTSCheatAction.h"
#include "Cheat/AsceCheatLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRTSCheatLibrary)

bool UAsceRTSCheatLibrary::RunFromLine(UObject* WorldContextObject, APlayerController* Requester,
	const FString& Line, FString& OutSummary, FString& OutError)
{
	return UAsceCheatLibrary::RunFromLine(WorldContextObject, Requester,
		UAsceRTSCheatAction::StaticClass(), Line, OutSummary, OutError);
}
