// Fill out your copyright notice in the Description page of Project Settings.

// Copyright Epic Games, Inc. All Rights Reserved.

#include "AscePlayerCameraManager.h"

#include "Async/TaskGraphInterfaces.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
//#include "AsceCameraComponent.h"
#include "AsceUICameraManagerComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AscePlayerCameraManager)

class FDebugDisplayInfo;

static FName UICameraComponentName(TEXT("UICamera"));

AAscePlayerCameraManager::AAscePlayerCameraManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultFOV = ASCE_CAMERA_DEFAULT_FOV;
	ViewPitchMin = ASCE_CAMERA_DEFAULT_PITCH_MIN;
	ViewPitchMax = ASCE_CAMERA_DEFAULT_PITCH_MAX;

	UICamera = CreateDefaultSubobject<UAsceUICameraManagerComponent>(UICameraComponentName);
}

UAsceUICameraManagerComponent* AAscePlayerCameraManager::GetUICameraComponent() const
{
	return UICamera;
}

void AAscePlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	// If the UI Camera is looking at something, let it have priority.
	if (UICamera->NeedsToUpdateViewTarget())
	{
		Super::UpdateViewTarget(OutVT, DeltaTime);
		UICamera->UpdateViewTarget(OutVT, DeltaTime);
		return;
	}

	Super::UpdateViewTarget(OutVT, DeltaTime);
}

void AAscePlayerCameraManager::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	check(Canvas);

	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;

	DisplayDebugManager.SetFont(GEngine->GetSmallFont());
	DisplayDebugManager.SetDrawColor(FColor::Yellow);
	DisplayDebugManager.DrawString(FString::Printf(TEXT("LyraPlayerCameraManager: %s"), *GetNameSafe(this)));

	Super::DisplayDebug(Canvas, DebugDisplay, YL, YPos);

	const APawn* Pawn = (PCOwner ? PCOwner->GetPawn() : nullptr);

	// if (const UAsceCameraComponent* CameraComponent = UAsceCameraComponent::FindCameraComponent(Pawn))
	// {
	// 	CameraComponent->DrawDebug(Canvas);
	// }
}

