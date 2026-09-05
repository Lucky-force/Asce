// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceGameSettingRegistry.h"

#include "GameSettingCollection.h"
#include "AsceSettingsLocal.h"
#include "AsceSettingsShared.h"
#include "Player/AsceLocalPlayer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceGameSettingRegistry)

DEFINE_LOG_CATEGORY(LogAsceGameSettingRegistry);

#define LOCTEXT_NAMESPACE "Asce"

//--------------------------------------
// UAsceGameSettingRegistry
//--------------------------------------

UAsceGameSettingRegistry::UAsceGameSettingRegistry()
{
}

UAsceGameSettingRegistry* UAsceGameSettingRegistry::Get(UAsceLocalPlayer* InLocalPlayer)
{
	UAsceGameSettingRegistry* Registry = FindObject<UAsceGameSettingRegistry>(InLocalPlayer, TEXT("AsceGameSettingRegistry"), EFindObjectFlags::ExactClass);
	if (Registry == nullptr)
	{
		Registry = NewObject<UAsceGameSettingRegistry>(InLocalPlayer, TEXT("AsceGameSettingRegistry"));
		Registry->Initialize(InLocalPlayer);
	}

	return Registry;
}

bool UAsceGameSettingRegistry::IsFinishedInitializing() const
{
	if (Super::IsFinishedInitializing())
	{
		if (UAsceLocalPlayer* LocalPlayer = Cast<UAsceLocalPlayer>(OwningLocalPlayer))
		{
			if (LocalPlayer->GetSharedSettings() == nullptr)
			{
				return false;
			}
		}

		return true;
	}

	return false;
}

void UAsceGameSettingRegistry::OnInitialize(ULocalPlayer* InLocalPlayer)
{
	UAsceLocalPlayer* AsceLocalPlayer = Cast<UAsceLocalPlayer>(InLocalPlayer);

	// VideoSettings = InitializeVideoSettings(AsceLocalPlayer);
	// InitializeVideoSettings_FrameRates(VideoSettings, AsceLocalPlayer);
	// RegisterSetting(VideoSettings);
	//
	// AudioSettings = InitializeAudioSettings(AsceLocalPlayer);
	// RegisterSetting(AudioSettings);
	//
	// GameplaySettings = InitializeGameplaySettings(AsceLocalPlayer);
	// RegisterSetting(GameplaySettings);
	//
	// MouseAndKeyboardSettings = InitializeMouseAndKeyboardSettings(AsceLocalPlayer);
	// RegisterSetting(MouseAndKeyboardSettings);
	//
	// GamepadSettings = InitializeGamepadSettings(AsceLocalPlayer);
	// RegisterSetting(GamepadSettings);
}

void UAsceGameSettingRegistry::SaveChanges()
{
	Super::SaveChanges();
	
	if (UAsceLocalPlayer* LocalPlayer = Cast<UAsceLocalPlayer>(OwningLocalPlayer))
	{
		// Game user settings need to be applied to handle things like resolution, this saves indirectly
		LocalPlayer->GetLocalSettings()->ApplySettings(false);
		
		//LocalPlayer->GetSharedSettings()->ApplySettings();
		//LocalPlayer->GetSharedSettings()->SaveSettings();
	}
}

#undef LOCTEXT_NAMESPACE

