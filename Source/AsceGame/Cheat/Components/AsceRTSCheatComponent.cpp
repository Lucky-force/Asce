// Fill out your copyright notice in the Description page of Project Settings.

#include "Cheat/Components/AsceRTSCheatComponent.h"

#include "Cheat/Actions/RTS/AsceRTSCheatLibrary.h"
#include "Cheat/Actions/RTS/AsceRTSCheatOps.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRTSCheatComponent)

bool UAsceRTSCheatComponent::ExecuteLine(const FString& Line, const bool bHasCursor
	, const FVector& CursorWorldLocation, FString& OutSummary, FString& OutError)
{
	return UAsceRTSCheatLibrary::RunFromLineAt(this, GetOwnerPlayerController(),
		Line, bHasCursor, CursorWorldLocation, OutSummary, OutError);
}

bool UAsceRTSCheatComponent::GetRequestCursor(FVector& OutCursorWorldLocation) const
{
	FString Error;
	return AsceRTSCheatOps::GetCursorWorldLocation(GetOwnerPlayerController(), OutCursorWorldLocation, Error);
}
