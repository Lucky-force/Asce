// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Subsystems/EngineSubsystem.h"
#include "AsceExperienceManager.generated.h"

/**
 * 这个子系统用来在编辑器下记录插件的使用情况
 */
UCLASS(MinimalAPI)
class UAsceExperienceManager : public UEngineSubsystem
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	ASCEGAME_API void OnPlayInEditorBegun();

	static void NotifyOfPluginActivation(const FString PluginURL);
	static bool RequestToDeactivatePlugin(const FString PluginURL);
#else
	static void NotifyOfPluginActivation(const FString PluginURL) {}
	static bool RequestToDeactivatePlugin(const FString PluginURL) { return true; }
#endif

private:
	// 用来记录插件的使用计数
	// (仅在编辑器下使用，首次使用某个插件时加入字典，最后一个被取消激活时从字典中移除)
	TMap<FString, int32> GameFeaturePluginRequestCountMap;
	
};
