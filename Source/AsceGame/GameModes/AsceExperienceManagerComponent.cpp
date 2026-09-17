#include "AsceExperienceManagerComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "AsceExperienceDefinition.h"
#include "AsceExperienceActionSet.h"
#include "AsceExperienceManager.h"
#include "GameFeaturesSubsystem.h"
#include "System/AsceAssetManager.h"
#include "GameFeatureAction.h"
#include "GameFeaturesSubsystemSettings.h"
#include "TimerManager.h"
#include "Settings/AsceSettingsLocal.h"
#include "AsceLogChannels.h"
#include "AsceMass/Simulation/AsceSimulationSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceExperienceManagerComponent)

//@TODO: Async load the experience definition itself
//@TODO: Handle failures explicitly (go into a 'completed but failed' state rather than check()-ing)
//@TODO: Do the action phases at the appropriate times instead of all at once
//@TODO: Support deactivating an experience and do the unloading actions
//@TODO: Think about what deactivation/cleanup means for preloaded assets
//@TODO: Handle deactivating game features, right now we 'leak' them enabled
// (for a client moving from experience to experience we actually want to diff the requirements and only unload some, not unload everything for them to just be immediately reloaded)
//@TODO: Handle both built-in and URL-based plugins (search for colon?)

/**TODO 这里有一堆TODO可以之后改进
 * 让ExperienceDefinition自己异步加载
 * 显示的处理加载失败的情况（现在是直接check，加载失败就直接崩溃了，让其进入一个completed but failed状态是更好的选择）
 * 分帧处理action的应用，现在是在加载的这一帧直接全部作用了，可能会引起卡顿
 * 现在的Experience只有一个Deactivating状态，实际上并没有实装，可以支持这一行为并且卸载Experience以及其所作的Actions
 * 对于预加载的资产，Deactivation和CleaUp应该进行哪些处理？
 * 处理deactivating gamefeature，现在是直接让他们泄露了（一直激活，等待世界销毁）
 * 对于客户端从一种experience的游戏移动到另一个experience的游戏时，实际上希望比较两个experience的不同，仅加载额外的数据以及卸载多余的数据，现在是直接全部卸载了然后重新加载
 * 处理构建加入的插件和使用URL加入的插件
 */



namespace AsceConsoleVariables
{
	static float ExperienceLoadRandomDelayMin = 0.0f;
	static FAutoConsoleVariableRef CVarExperienceLoadRandomDelayMin(
		TEXT("asce.chaos.ExperienceDelayLoad.MinSecs"),
		ExperienceLoadRandomDelayMin,
		TEXT("This value (in seconds) will be added as a delay of load completion of the experience (along with the random value asce.chaos.ExperienceDelayLoad.RandomSecs)"),
		ECVF_Default);

	static float ExperienceLoadRandomDelayRange = 0.0f;
	static FAutoConsoleVariableRef CVarExperienceLoadRandomDelayRange(
		TEXT("asce.chaos.ExperienceDelayLoad.RandomSecs"),
		ExperienceLoadRandomDelayRange,
		TEXT("A random amount of time between 0 and this value (in seconds) will be added as a delay of load completion of the experience (along with the fixed value asce.chaos.ExperienceDelayLoad.MinSecs)"),
		ECVF_Default);

	float GetExperienceLoadDelayDuration()
	{
		return FMath::Max(0.0f, ExperienceLoadRandomDelayMin + FMath::FRand() * ExperienceLoadRandomDelayRange);
	}
}

UAsceExperienceManagerComponent::UAsceExperienceManagerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
}

void UAsceExperienceManagerComponent::SetCurrentExperience(FPrimaryAssetId ExperienceId)
{
	UAsceAssetManager& AssetManager = UAsceAssetManager::Get();
	FSoftObjectPath AssetPath = AssetManager.GetPrimaryAssetPath(ExperienceId);
	
	
	TSubclassOf<UAsceExperienceDefinition> AssetClass = Cast<UClass>(AssetPath.TryLoad());//类加载不用AssetManager进行管理？
	//似乎即使是主资产，像Experience这种不会频繁加载卸载的资产就直接TryLoad了
	//类加载不需要手动维护引用来防止gc，class和cdo强绑定，cdo被全局引擎引用，因此二者除非主动卸载，否则不会自动gc
	
	check(AssetClass);
	const UAsceExperienceDefinition* Experience = GetDefault<UAsceExperienceDefinition>(AssetClass);

	check(Experience != nullptr);
	check(CurrentExperience == nullptr);
	CurrentExperience = Experience;
	StartExperienceLoad();
}

void UAsceExperienceManagerComponent::CallOrRegister_OnExperienceLoaded_HighPriority(FOnAsceExperienceLoaded::FDelegate&& Delegate)
{
	if (IsExperienceLoaded())
	{
		Delegate.Execute(CurrentExperience);
	}
	else
	{
		OnExperienceLoaded_HighPriority.Add(MoveTemp(Delegate));
	}
}

void UAsceExperienceManagerComponent::CallOrRegister_OnExperienceLoaded(FOnAsceExperienceLoaded::FDelegate&& Delegate)
{
	if (IsExperienceLoaded())
	{
		Delegate.Execute(CurrentExperience);
	}
	else
	{
		OnExperienceLoaded.Add(MoveTemp(Delegate));
	}
}

void UAsceExperienceManagerComponent::CallOrRegister_OnExperienceLoaded_LowPriority(FOnAsceExperienceLoaded::FDelegate&& Delegate)
{
	if (IsExperienceLoaded())
	{
		Delegate.Execute(CurrentExperience);
	}
	else
	{
		OnExperienceLoaded_LowPriority.Add(MoveTemp(Delegate));
	}
}

const UAsceExperienceDefinition* UAsceExperienceManagerComponent::GetCurrentExperienceChecked() const
{
	//保证必须在加载完成之后才能调用这个函数
	check(LoadState == EAsceExperienceLoadState::Loaded);
	check(CurrentExperience != nullptr);
	return CurrentExperience;
}

bool UAsceExperienceManagerComponent::IsExperienceLoaded() const
{
	return (LoadState == EAsceExperienceLoadState::Loaded) && (CurrentExperience != nullptr);
}

void UAsceExperienceManagerComponent::OnRep_CurrentExperience()
{
	StartExperienceLoad();
}

void UAsceExperienceManagerComponent::StartExperienceLoad()
{
	//对于服务端，CurrentExperience从CDO取过来后立即调用此函数开始正式加载流程
	//对于客户端，CurrentExperience从服务端复制过来时开始加载流程
	//两种情况都必须保证CurrentExperience的存在, 并且状态必须是Unloaded
	check(CurrentExperience != nullptr);
	check(LoadState == EAsceExperienceLoadState::Unloaded);

	UE_LOG(LogAsceExperience, Log, TEXT("EXPERIENCE: StartExperienceLoad(CurrentExperience = %s, %s)"),
		*CurrentExperience->GetPrimaryAssetId().ToString(),
		*GetClientServerContextString(this));

	LoadState = EAsceExperienceLoadState::Loading;

	UAsceAssetManager& AssetManager = UAsceAssetManager::Get();

	TSet<FPrimaryAssetId> BundleAssetList;//要加载的主资产列表
	TSet<FSoftObjectPath> RawAssetList;//要加载的普通资产列表

	//这里有点多此一举了吧，硬引用应该直接加载了的
	BundleAssetList.Add(CurrentExperience->GetPrimaryAssetId());
	for (const TObjectPtr<UAsceExperienceActionSet>& ActionSet : CurrentExperience->ActionSets)
	{
		if (ActionSet != nullptr)
		{
			BundleAssetList.Add(ActionSet->GetPrimaryAssetId());
		}
	}

	// Load assets associated with the experience
	// 加载和Experience相关的Bundles，这里默认加载Equipped，同时根据客户端和服务端加载不同的资产
	TArray<FName> BundlesToLoad;
	BundlesToLoad.Add(FAsceBundles::Equipped);

	//@TODO: Centralize this client/server stuff into the AsceAssetManager
	const ENetMode OwnerNetMode = GetOwner()->GetNetMode();
	const bool bLoadClient = GIsEditor || (OwnerNetMode != NM_DedicatedServer);
	const bool bLoadServer = GIsEditor || (OwnerNetMode != NM_Client);
	if (bLoadClient)
	{
		BundlesToLoad.Add(UGameFeaturesSubsystemSettings::LoadStateClient);
	}
	if (bLoadServer)
	{
		BundlesToLoad.Add(UGameFeaturesSubsystemSettings::LoadStateServer);
	}

	TSharedPtr<FStreamableHandle> BundleLoadHandle = nullptr;
	if (BundleAssetList.Num() > 0)
	{
		BundleLoadHandle = AssetManager.ChangeBundleStateForPrimaryAssets(BundleAssetList.Array(), BundlesToLoad, {}, false, FStreamableDelegate(), FStreamableManager::AsyncLoadHighPriority);
	}

	TSharedPtr<FStreamableHandle> RawLoadHandle = nullptr;
	if (RawAssetList.Num() > 0)
	{
		RawLoadHandle = AssetManager.LoadAssetList(RawAssetList.Array(), FStreamableDelegate(), FStreamableManager::AsyncLoadHighPriority, TEXT("StartExperienceLoad()"));
	}

	// If both async loads are running, combine them
	// 主资产和普通资产的加载都是异步的，这里可以把两个句柄进行合并
	TSharedPtr<FStreamableHandle> Handle = nullptr;
	if (BundleLoadHandle.IsValid() && RawLoadHandle.IsValid())
	{
		Handle = AssetManager.GetStreamableManager().CreateCombinedHandle({ BundleLoadHandle, RawLoadHandle });
	}
	else
	{
		Handle = BundleLoadHandle.IsValid() ? BundleLoadHandle : RawLoadHandle;
	}

	FStreamableDelegate OnAssetsLoadedDelegate = FStreamableDelegate::CreateUObject(this, &ThisClass::OnExperienceLoadComplete);
	if (!Handle.IsValid() || Handle->HasLoadCompleted())
	{
		// Assets were already loaded, call the delegate now
		FStreamableHandle::ExecuteDelegate(OnAssetsLoadedDelegate);
	}
	else
	{
		Handle->BindCompleteDelegate(OnAssetsLoadedDelegate);

		Handle->BindCancelDelegate(FStreamableDelegate::CreateLambda([OnAssetsLoadedDelegate]()
			{
				OnAssetsLoadedDelegate.ExecuteIfBound();
			}));
	}

	// This set of assets gets preloaded, but we don't block the start of the experience based on it
	TSet<FPrimaryAssetId> PreloadAssetList;
	//@TODO: Determine assets to preload (but not blocking-ly)
	if (PreloadAssetList.Num() > 0)
	{
		AssetManager.ChangeBundleStateForPrimaryAssets(PreloadAssetList.Array(), BundlesToLoad, {});
	}
}

void UAsceExperienceManagerComponent::OnExperienceLoadComplete()
{
	check(LoadState == EAsceExperienceLoadState::Loading);
	check(CurrentExperience != nullptr);

	UE_LOG(LogAsceExperience, Log, TEXT("EXPERIENCE: OnExperienceLoadComplete(CurrentExperience = %s, %s)"),
		*CurrentExperience->GetPrimaryAssetId().ToString(),
		*GetClientServerContextString(this));

	
	// find the URLs for our GameFeaturePlugins - filtering out dupes and ones that don't have a valid mapping
	// 获取当前GameFeature对应的GameFeature插件url（每个GameFeature都是一个插件）
	GameFeaturePluginURLs.Reset();

	auto CollectGameFeaturePluginURLs = [This=this](const UPrimaryDataAsset* Context, const TArray<FString>& FeaturePluginList)
	{
		for (const FString& PluginName : FeaturePluginList)
		{
			FString PluginURL;
			if (UGameFeaturesSubsystem::Get().GetPluginURLByName(PluginName, /*out*/ PluginURL))
			{
				This->GameFeaturePluginURLs.AddUnique(PluginURL);
			}
			else
			{
				ensureMsgf(false, TEXT("OnExperienceLoadComplete failed to find plugin URL from PluginName %s for experience %s - fix data, ignoring for this run"), *PluginName, *Context->GetPrimaryAssetId().ToString());
			}
		}

		// Add in our extra plugin
		// if (!CurrentPlaylistData->GameFeaturePluginToActivateUntilDownloadedContentIsPresent.IsEmpty())
		// {
		// 	FString PluginURL;
		// 	if (UGameFeaturesSubsystem::Get().GetPluginURLByName(CurrentPlaylistData->GameFeaturePluginToActivateUntilDownloadedContentIsPresent, PluginURL))
		// 	{
		// 		GameFeaturePluginURLs.AddUnique(PluginURL);
		// 	}
		// }
	};

	CollectGameFeaturePluginURLs(CurrentExperience, CurrentExperience->GameFeaturesToEnable);
	
	
	for (const TObjectPtr<UAsceExperienceActionSet>& ActionSet : CurrentExperience->ActionSets)
	{
		if (ActionSet != nullptr)
		{
			CollectGameFeaturePluginURLs(ActionSet, ActionSet->GameFeaturesToEnable);
		}
	}

	// Load and activate the features
	// 加载Experience所需要的GameFeatures
	NumGameFeaturePluginsLoading = GameFeaturePluginURLs.Num();
	if (NumGameFeaturePluginsLoading > 0)
	{
		LoadState = EAsceExperienceLoadState::LoadingGameFeatures;
		for (const FString& PluginURL : GameFeaturePluginURLs)
		{
			UAsceExperienceManager::NotifyOfPluginActivation(PluginURL);
			UGameFeaturesSubsystem::Get().LoadAndActivateGameFeaturePlugin(PluginURL, FGameFeaturePluginLoadComplete::CreateUObject(this, &ThisClass::OnGameFeaturePluginLoadComplete));
		}
	}
	else
	{
		OnExperienceFullLoadCompleted();
	}
}

void UAsceExperienceManagerComponent::OnGameFeaturePluginLoadComplete(const UE::GameFeatures::FResult& Result)
{
	// decrement the number of plugins that are loading
	NumGameFeaturePluginsLoading--;

	if (NumGameFeaturePluginsLoading == 0)
	{
		OnExperienceFullLoadCompleted();
	}
}

void UAsceExperienceManagerComponent::OnExperienceFullLoadCompleted()
{
	check(LoadState != EAsceExperienceLoadState::Loaded);

	// Insert a random delay for testing (if configured)
	// 添加一个随机的延迟来测试，不然瞬间就到ExecutingActions状态了
	if (LoadState != EAsceExperienceLoadState::LoadingChaosTestingDelay)
	{
		const float DelaySecs = AsceConsoleVariables::GetExperienceLoadDelayDuration();
		if (DelaySecs > 0.0f)
		{
			FTimerHandle DummyHandle;

			LoadState = EAsceExperienceLoadState::LoadingChaosTestingDelay;
			GetWorld()->GetTimerManager().SetTimer(DummyHandle, this, &ThisClass::OnExperienceFullLoadCompleted, DelaySecs, /*bLooping=*/ false);

			return;
		}
	}

	LoadState = EAsceExperienceLoadState::ExecutingActions;

	// Execute the actions
	FGameFeatureActivatingContext Context;

	// Only apply to our specific world context if set
	const FWorldContext* ExistingWorldContext = GEngine->GetWorldContextFromWorld(GetWorld());
	if (ExistingWorldContext)
	{
		Context.SetRequiredWorldContextHandle(ExistingWorldContext->ContextHandle);
	}

	auto ActivateListOfActions = [&Context](const TArray<UGameFeatureAction*>& ActionList)
	{
		for (UGameFeatureAction* Action : ActionList)
		{
			if (Action != nullptr)
			{
				//@TODO: The fact that these don't take a world are potentially problematic in client-server PIE
				// The current behavior matches systems like gameplay tags where loading and registering apply to the entire process,
				// but actually applying the results to actors is restricted to a specific world
				Action->OnGameFeatureRegistering();
				Action->OnGameFeatureLoading();
				Action->OnGameFeatureActivating(Context);
			}
		}
	};

	ActivateListOfActions(CurrentExperience->Actions);
	for (const TObjectPtr<UAsceExperienceActionSet>& ActionSet : CurrentExperience->ActionSets)
	{
		//GameFeature中的Actions会随着GameFeature的加载流程自动切换状态
		//Experience中的额外Action就只能手动切换了
		if (ActionSet != nullptr)
		{
			ActivateListOfActions(ActionSet->Actions);
		}
	}

	LoadState = EAsceExperienceLoadState::Loaded;

	// Asce 模拟：loading 结束，允许调度器开始跑阶段链（世界数据初始化在此之前已完成，见 ShouldShowLoadingScreen）
	if (UAsceSimulationSubsystem* Sim = UAsceSimulationSubsystem::Get(GetWorld()))
	{
		Sim->NotifyLoadingFinished();
	}

	OnExperienceLoaded_HighPriority.Broadcast(CurrentExperience);
	OnExperienceLoaded_HighPriority.Clear();

	OnExperienceLoaded.Broadcast(CurrentExperience);
	OnExperienceLoaded.Clear();

	OnExperienceLoaded_LowPriority.Broadcast(CurrentExperience);
	OnExperienceLoaded_LowPriority.Clear();

	// Apply any necessary scalability settings
#if !UE_SERVER
	UAsceSettingsLocal::Get()->OnExperienceLoaded();
#endif
}

void UAsceExperienceManagerComponent::OnActionDeactivationCompleted()
{
	check(IsInGameThread());
	++NumObservedPausers;

	if (NumObservedPausers == NumExpectedPausers)
	{
		OnAllActionsDeactivated();
	}
}

void UAsceExperienceManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, CurrentExperience);
}

void UAsceExperienceManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// deactivate any features this experience loaded
	//@TODO: This should be handled FILO as well
	for (const FString& PluginURL : GameFeaturePluginURLs)
	{
		if (UAsceExperienceManager::RequestToDeactivatePlugin(PluginURL))
		{
			UGameFeaturesSubsystem::Get().DeactivateGameFeaturePlugin(PluginURL);
		}
	}

	//@TODO: Ensure proper handling of a partially-loaded state too
	if (LoadState == EAsceExperienceLoadState::Loaded)
	{
		LoadState = EAsceExperienceLoadState::Deactivating;

		// Make sure we won't complete the transition prematurely if someone registers as a pauser but fires immediately
		NumExpectedPausers = INDEX_NONE;
		NumObservedPausers = 0;

		// Deactivate and unload the actions
		FGameFeatureDeactivatingContext Context(TEXT(""), [this](FStringView) { this->OnActionDeactivationCompleted(); });

		const FWorldContext* ExistingWorldContext = GEngine->GetWorldContextFromWorld(GetWorld());
		if (ExistingWorldContext)
		{
			Context.SetRequiredWorldContextHandle(ExistingWorldContext->ContextHandle);
		}

		auto DeactivateListOfActions = [&Context](const TArray<UGameFeatureAction*>& ActionList)
		{
			for (UGameFeatureAction* Action : ActionList)
			{
				if (Action)
				{
					Action->OnGameFeatureDeactivating(Context);
					Action->OnGameFeatureUnregistering();
				}
			}
		};

		DeactivateListOfActions(CurrentExperience->Actions);
		for (const TObjectPtr<UAsceExperienceActionSet>& ActionSet : CurrentExperience->ActionSets)
		{
			if (ActionSet != nullptr)
			{
				DeactivateListOfActions(ActionSet->Actions);
			}
		}

		NumExpectedPausers = Context.GetNumPausers();

		if (NumExpectedPausers > 0)
		{
			UE_LOG(LogAsceExperience, Error, TEXT("Actions that have asynchronous deactivation aren't fully supported yet in Lyra experiences"));
		}

		if (NumExpectedPausers == NumObservedPausers)
		{
			OnAllActionsDeactivated();
		}
	}
}

bool UAsceExperienceManagerComponent::ShouldShowLoadingScreen(FString& OutReason) const
{
	if (LoadState != EAsceExperienceLoadState::Loaded)
	{
		OutReason = TEXT("Experience still loading");
		return true;
	}

	// 世界数据（地图尺寸/代价场/墙簇/视野高度场/空间网格）还没初始化完 ⇒ 继续收着 loading 界面。
	// 初始化由 UAsceSimulationSubsystem 在 AAsceWorldMapReplicatorActor 注册时完成（fail-closed：
	// 关卡里没放这个 Actor 就永远初始化不了 ⇒ 界面不消失，日志里会有一条 Error 说明原因）。
	if (const UAsceSimulationSubsystem* Sim = UAsceSimulationSubsystem::Get(GetWorld()))
	{
		if (!Sim->IsWorldInitialized())
		{
			OutReason = TEXT("Asce world data not initialized");
			return true;
		}
	}

	return false;
}

void UAsceExperienceManagerComponent::OnAllActionsDeactivated()
{
	//@TODO: We actually only deactivated and didn't fully unload...
	LoadState = EAsceExperienceLoadState::Unloaded;
	CurrentExperience = nullptr;
	//@TODO:	GEngine->ForceGarbageCollection(true);
}

