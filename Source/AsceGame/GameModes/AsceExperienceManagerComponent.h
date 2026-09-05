
#pragma once

#include "Components/GameStateComponent.h"
#include "LoadingProcessInterface.h"

#include "AsceExperienceManagerComponent.generated.h"

#define UE_API ASCEGAME_API

namespace UE::GameFeatures { struct FResult; }

class UAsceExperienceDefinition;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsceExperienceLoaded, const UAsceExperienceDefinition* /*Experience*/);

enum class EAsceExperienceLoadState
{
	Unloaded,
	Loading,
	LoadingGameFeatures,
	LoadingChaosTestingDelay,
	ExecutingActions,
	Loaded,
	Deactivating
};


//挂在GameState上面的全局Component，负责管理Experience
//ILoadingProcessInterface来自LoadingScreen插件，用来处理是否显示加载界面的接口
//Expeience和GameMode相关的这些必须加载完成之后才能让玩家正式进入游戏

UCLASS(MinimalAPI)
class UAsceExperienceManagerComponent final : public UGameStateComponent, public ILoadingProcessInterface
{
	GENERATED_BODY()

public:

	UE_API UAsceExperienceManagerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UActorComponent interface
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~End of UActorComponent interface

	//~ILoadingProcessInterface interface
	UE_API virtual bool ShouldShowLoadingScreen(FString& OutReason) const override;
	//~End of ILoadingProcessInterface

	// Tries to set the current experience, either a UI or gameplay one
	UE_API void SetCurrentExperience(FPrimaryAssetId ExperienceId);

	// Ensures the delegate is called once the experience has been loaded,
	// before others are called.
	// However, if the experience has already loaded, calls the delegate immediately.
	// 高优先级，确保一旦experience被加载就立即广播(在其他两个优先级之前)。如果在注册的时候已经加载好了也立即广播
	UE_API void CallOrRegister_OnExperienceLoaded_HighPriority(FOnAsceExperienceLoaded::FDelegate&& Delegate);

	// Ensures the delegate is called once the experience has been loaded
	// If the experience has already loaded, calls the delegate immediately
	// 在低优先级之前，高优先级之后广播
	UE_API void CallOrRegister_OnExperienceLoaded(FOnAsceExperienceLoaded::FDelegate&& Delegate);

	// Ensures the delegate is called once the experience has been loaded
	// If the experience has already loaded, calls the delegate immediately
	// 最后广播
	UE_API void CallOrRegister_OnExperienceLoaded_LowPriority(FOnAsceExperienceLoaded::FDelegate&& Delegate);

	// This returns the current experience if it is fully loaded, asserting otherwise
	// (i.e., if you called it too soon)
	// 必须在加载完成后才能调用此函数，否则直接崩溃吧
	UE_API const UAsceExperienceDefinition* GetCurrentExperienceChecked() const;

	// Returns true if the experience is fully loaded
	UE_API bool IsExperienceLoaded() const;

private:
	UFUNCTION()
	void OnRep_CurrentExperience();

	void StartExperienceLoad();
	void OnExperienceLoadComplete();
	void OnGameFeaturePluginLoadComplete(const UE::GameFeatures::FResult& Result);
	void OnExperienceFullLoadCompleted();

	void OnActionDeactivationCompleted();
	void OnAllActionsDeactivated();

private:
	UPROPERTY(ReplicatedUsing=OnRep_CurrentExperience)
	TObjectPtr<const UAsceExperienceDefinition> CurrentExperience;

	EAsceExperienceLoadState LoadState = EAsceExperienceLoadState::Unloaded;

	int32 NumGameFeaturePluginsLoading = 0;
	TArray<FString> GameFeaturePluginURLs;

	int32 NumObservedPausers = 0;
	int32 NumExpectedPausers = 0;

	/**
	 * Delegate called when the experience has finished loading just before others
	 * (e.g., subsystems that set up for regular gameplay)
	 * 这些委托仅仅在Experience加载完成后按优先级依次广播并清空。
	 * 主要是各个系统之间可能有依赖性，比如系统B的OnExperiencLoaded的处理必须在系统A完成之后才能进行
	 * 直接在系统B里面监听系统A会增加耦合性，将系统A的回调绑定到高优先级，系统B的绑定到普通优先级，能保证系统A的回调一定比系统B先执行
	 */
	FOnAsceExperienceLoaded OnExperienceLoaded_HighPriority;

	/** Delegate called when the experience has finished loading */
	FOnAsceExperienceLoaded OnExperienceLoaded;

	/** Delegate called when the experience has finished loading */
	FOnAsceExperienceLoaded OnExperienceLoaded_LowPriority;
};

#undef UE_API
