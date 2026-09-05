// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "DataSource/GameSettingDataSourceDynamic.h" // IWYU pragma: keep
#include "GameSettingRegistry.h"
#include "Settings/AsceSettingsLocal.h" // IWYU pragma: keep

#include "AsceGameSettingRegistry.generated.h"

class ULocalPlayer;
class UObject;

//--------------------------------------
// UAsceGameSettingRegistry
//--------------------------------------

class UGameSettingCollection;
class UAsceLocalPlayer;

DECLARE_LOG_CATEGORY_EXTERN(LogAsceGameSettingRegistry, Log, Log);

#define GET_SHARED_SETTINGS_FUNCTION_PATH(FunctionOrPropertyName)							\
	MakeShared<FGameSettingDataSourceDynamic>(TArray<FString>({								\
		GET_FUNCTION_NAME_STRING_CHECKED(UAsceLocalPlayer, GetSharedSettings),				\
		GET_FUNCTION_NAME_STRING_CHECKED(UAsceSettingsShared, FunctionOrPropertyName)		\
	}))

#define GET_LOCAL_SETTINGS_FUNCTION_PATH(FunctionOrPropertyName)							\
	MakeShared<FGameSettingDataSourceDynamic>(TArray<FString>({								\
		GET_FUNCTION_NAME_STRING_CHECKED(UAsceLocalPlayer, GetLocalSettings),				\
		GET_FUNCTION_NAME_STRING_CHECKED(UAsceSettingsLocal, FunctionOrPropertyName)		\
	}))

/**
 * 
 */
UCLASS()
class UAsceGameSettingRegistry : public UGameSettingRegistry
{
	GENERATED_BODY()

public:
	UAsceGameSettingRegistry();

	static UAsceGameSettingRegistry* Get(UAsceLocalPlayer* InLocalPlayer);
	
	virtual void SaveChanges() override;

protected:
	virtual void OnInitialize(ULocalPlayer* InLocalPlayer) override;
	virtual bool IsFinishedInitializing() const override;

	// UGameSettingCollection* InitializeVideoSettings(UAsceLocalPlayer* InLocalPlayer);
	// void InitializeVideoSettings_FrameRates(UGameSettingCollection* Screen, UAsceLocalPlayer* InLocalPlayer);
	// void AddPerformanceStatPage(UGameSettingCollection* Screen, UAsceLocalPlayer* InLocalPlayer);
	//
	// UGameSettingCollection* InitializeAudioSettings(UAsceLocalPlayer* InLocalPlayer);
	// UGameSettingCollection* InitializeGameplaySettings(UAsceLocalPlayer* InLocalPlayer);
	//
	// UGameSettingCollection* InitializeMouseAndKeyboardSettings(UAsceLocalPlayer* InLocalPlayer);
	// UGameSettingCollection* InitializeGamepadSettings(UAsceLocalPlayer* InLocalPlayer);

	UPROPERTY()
	TObjectPtr<UGameSettingCollection> VideoSettings;

	UPROPERTY()
	TObjectPtr<UGameSettingCollection> AudioSettings;

	UPROPERTY()
	TObjectPtr<UGameSettingCollection> GameplaySettings;

	UPROPERTY()
	TObjectPtr<UGameSettingCollection> MouseAndKeyboardSettings;

	UPROPERTY()
	TObjectPtr<UGameSettingCollection> GamepadSettings;
};
