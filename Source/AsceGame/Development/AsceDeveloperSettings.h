// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine/DeveloperSettingsBackedByCVars.h"
#include "UObject/PrimaryAssetId.h"
#include "UObject/SoftObjectPath.h"
#include "AsceDeveloperSettings.generated.h"

struct FPropertyChangedEvent;

class ULyraExperienceDefinition;

UENUM()
enum class ECheatExecutionTime
{
	// cheat manager 创建时执行
	OnCheatManagerCreated,

	// 当玩家占据角色的时候执行
	OnPlayerPawnPossession
};

USTRUCT()
struct FAsceCheatToRun
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	ECheatExecutionTime Phase = ECheatExecutionTime::OnPlayerPawnPossession;

	UPROPERTY(EditAnywhere)
	FString Cheat;
};

/**
 * Developer settings / editor cheats
 * 开发者设置/编辑器作弊器
 * 这种类都是不可修改的，内部保存了数据，而其他地方都只是访问其中的数据或者调用其中的函数
 * 因此总是使用cdo
 */
UCLASS(config=EditorPerProjectUserSettings, MinimalAPI)
class UAsceDeveloperSettings : public UDeveloperSettingsBackedByCVars
{
	GENERATED_BODY()

public:
	UAsceDeveloperSettings();

	//~UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	//~End of UDeveloperSettings interface

public:
	// The experience override to use for Play in Editor (if not set, the default for the world settings of the open map will be used)
	// 在编辑器中运行时，希望重载的Experience的PrimaryAssetId， 如果没有设置，就会使用当前开启的地图的默认值
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, config, Category=Asce, meta=(AllowedTypes="AsceExperienceDefinition"))
	FPrimaryAssetId ExperienceOverride;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, config, Category=AsceBots, meta=(InlineEditConditionToggle))
	bool bOverrideBotCount = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, config, Category=AsceBots, meta=(EditCondition=bOverrideBotCount))
	int32 OverrideNumPlayerBotsToSpawn = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, config, Category=AsceBots)
	bool bAllowPlayerBotsToAttack = true;

	// Do the full game flow when playing in the editor, or skip 'waiting for player' / etc... game phases?
	// 是否在编辑器中运行时测试完整的游戏流程，或者跳过一些阶段
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, config, Category=Asce)
	bool bTestFullGameFlowInPIE = false;

	/**
	* Should force feedback effects be played, even if the last input device was not a gamepad?
	* The default behavior in Asce is to only play force feedback if the most recent input device was a gamepad.
	* 是否强制播放反馈效果，即使输入设备不是手柄，默认的行为是仅在最近的输入设备是手柄时才强制播放反馈
	*/
	UPROPERTY(config, EditAnywhere, Category = Asce, meta = (ConsoleVariable = "AscePC.ShouldAlwaysPlayForceFeedback"))
	bool bShouldAlwaysPlayForceFeedback = false;

	// Should game logic load cosmetic backgrounds in the editor or skip them for iteration speed?
	// 游戏逻辑是否在编辑器中加载自定义的背景？或者仅仅跳过他们来提升游戏的迭代速度
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, config, Category=Asce)
	bool bSkipLoadingCosmeticBackgroundsInPIE = false;

	// List of cheats to auto-run during 'play in editor'
	// 在编辑器中运行时默认使用的作弊指令
	UPROPERTY(config, EditAnywhere, Category=Asce)
	TArray<FAsceCheatToRun> CheatsToRun;
	
	// Should messages broadcast through the gameplay message subsystem be logged?
	// 在GameplayMessageSubsystem被启用时是否广播消息
	UPROPERTY(config, EditAnywhere, Category=GameplayMessages/*, meta=(ConsoleVariable="GameplayMessageSubsystem.LogMessages")*/)
	bool LogGameplayMessages = false;

#if WITH_EDITORONLY_DATA
	/** A list of common maps that will be accessible via the editor detoolbar */
	// 编辑器中直接可以快速访问的通用地图列表
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category=Maps, meta=(AllowedClasses="/Script/Engine.World"))
	TArray<FSoftObjectPath> CommonEditorMaps;
#endif
	
#if WITH_EDITOR
public:
	// Called by the editor engine to let us pop reminder notifications when cheats are active
	ASCEGAME_API void OnPlayInEditorStarted() const;

private:
	void ApplySettings();
#endif

public:
	//~UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostReloadConfig(FProperty* PropertyThatWasLoaded) override;
	virtual void PostInitProperties() override;
#endif
	//~End of UObject interface
};
