// Fill out your copyright notice in the Description page of Project Settings.


#include "AsceExperienceManager.h"
#include "Engine/Engine.h"
#include "Subsystems/SubsystemCollection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceExperienceManager)

#if WITH_EDITOR
void UAsceExperienceManager::OnPlayInEditorBegun()
{
	ensure(GameFeaturePluginRequestCountMap.IsEmpty());
	GameFeaturePluginRequestCountMap.Empty();
}

void UAsceExperienceManager::NotifyOfPluginActivation(const FString PluginURL)
{
	if (GIsEditor)
	{
		UAsceExperienceManager* ExperienceManager = GEngine->GetEngineSubsystem<UAsceExperienceManager>();
		check(ExperienceManager);
		int32& Counter = ExperienceManager->GameFeaturePluginRequestCountMap.FindOrAdd(PluginURL);
		++Counter;
	}
}

bool UAsceExperienceManager::RequestToDeactivatePlugin(const FString PluginURL)
{
	if (GIsEditor)
	{
		UAsceExperienceManager* ExperienceManager = GEngine->GetEngineSubsystem<UAsceExperienceManager>();
		check(ExperienceManager);
		int32& Counter = ExperienceManager->GameFeaturePluginRequestCountMap.FindChecked(PluginURL);
		--Counter;
		if (Counter == 0)
		{
			ExperienceManager->GameFeaturePluginRequestCountMap.Remove(PluginURL);
			return true;
		}
		return false;
	}
	return true;
}
#endif