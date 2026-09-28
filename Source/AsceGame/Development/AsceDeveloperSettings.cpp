// Fill out your copyright notice in the Description page of Project Settings.


#include "AsceDeveloperSettings.h"
#include "Misc/App.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceDeveloperSettings)
#define LOCTEXT_NAMESPACE "AsceCheats"

UAsceDeveloperSettings::UAsceDeveloperSettings()
{
}

FName UAsceDeveloperSettings::GetCategoryName() const
{
	return Super::GetCategoryName();
}

#if WITH_EDITOR
void UAsceDeveloperSettings::OnPlayInEditorStarted() const
{
	// if (ExperienceOverride.IsValid())
	// {
	// 	FNotificationInfo Info(FText::Format(
	// 		LOCTEXT("ExperienceOverrideActive", "Developer Settings Override\nExperience {0}"),
	// 		FText::FromName(ExperienceOverride.PrimaryAssetName)
	// 	));
	// 	Info.ExpireDuration = 2.0f;
	// 	FSlateNotificationManager::Get().AddNotification(Info);
	// }
}

void UAsceDeveloperSettings::ApplySettings()
{
	//待添加，当开发者设置生效时的自定义行为，将在属性修改，重新载入设置，初始化属性时调用
}

void UAsceDeveloperSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	ApplySettings();
}

void UAsceDeveloperSettings::PostReloadConfig(FProperty* PropertyThatWasLoaded)
{
	Super::PostReloadConfig(PropertyThatWasLoaded);
	ApplySettings();
}

void UAsceDeveloperSettings::PostInitProperties()
{
	Super::PostInitProperties();
	ApplySettings();
}

#endif
#undef LOCTEXT_NAMESPACE
