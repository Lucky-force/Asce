// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceUICameraManagerComponent.h"

#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "AscePlayerCameraManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceUICameraManagerComponent)

class AActor;
class FDebugDisplayInfo;

UAsceUICameraManagerComponent* UAsceUICameraManagerComponent::GetComponent(APlayerController* PC)
{
	if (PC != nullptr)
	{
		if (AAscePlayerCameraManager* PCCamera = Cast<AAscePlayerCameraManager>(PC->PlayerCameraManager))
		{
			return PCCamera->GetUICameraComponent();
		}
	}

	return nullptr;
}

UAsceUICameraManagerComponent::UAsceUICameraManagerComponent()
{
	bWantsInitializeComponent = true;

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// Register "showdebug" hook.
		if (!IsRunningDedicatedServer())
		{
			AHUD::OnShowDebugInfo.AddUObject(this, &ThisClass::OnShowDebugInfo);
		}
	}
}

void UAsceUICameraManagerComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

void UAsceUICameraManagerComponent::SetViewTarget(AActor* InViewTarget, FViewTargetTransitionParams TransitionParams)
{
	TGuardValue<bool> UpdatingViewTargetGuard(bUpdatingViewTarget, true);

	ViewTarget = InViewTarget;
	CastChecked<AAscePlayerCameraManager>(GetOwner())->SetViewTarget(ViewTarget, TransitionParams);
}

bool UAsceUICameraManagerComponent::NeedsToUpdateViewTarget() const
{
	return false;
}

void UAsceUICameraManagerComponent::UpdateViewTarget(struct FTViewTarget& OutVT, float DeltaTime)
{
}

void UAsceUICameraManagerComponent::OnShowDebugInfo(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos)
{
}
