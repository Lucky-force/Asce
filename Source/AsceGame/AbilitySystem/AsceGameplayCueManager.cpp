// Fill out your copyright notice in the Description page of Project Settings.
// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsceGameplayCueManager.h"
#include "Engine/AssetManager.h"
#include "AsceLogChannels.h"
#include "GameplayCueSet.h"
#include "AbilitySystemGlobals.h"
#include "GameplayTagsManager.h"
#include "UObject/UObjectThreadContext.h"
#include "Async/Async.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceGameplayCueManager)

//////////////////////////////////////////////////////////////////////

// 加载模式枚举
enum class EAsceEditorLoadMode
{
	// Loads all cues upfront; longer loading speed in the editor but short PIE times and effects never fail to play
	// 预先加载，会导致编辑器加载更久，但是在PIE时更快，并且Cue的播放永不失败
	LoadUpfront,

	// Outside of editor: Async loads as cue tag are registered
	// In editor: Async loads when cues are invoked
	//   Note: This can cause some 'why didn't I see the effect for X' issues in PIE and is good for iteration speed but otherwise bad for designers
	// 非编辑器时，当cue的tag注册时异步加载
	// 在编辑器中时，在cue播放时异步加载
	// 适用于编辑器中快速迭代，但是对设计师不友好(因为播放时异步加载可能导致某些cue的效果难以被看到)
	PreloadAsCuesAreReferenced_GameOnly,

	// Async loads as cue tag are registered
	// 无论在编辑器还是正式游戏中，总是在cue注册的时候异步加载
	PreloadAsCuesAreReferenced
};

namespace AsceGameplayCueManagerCvars
{
	static FAutoConsoleCommand CVarDumpGameplayCues(
		TEXT("Asce.DumpGameplayCues"),
		TEXT("Shows all assets that were loaded via AsceGameplayCueManager and are currently in memory."),
		FConsoleCommandWithArgsDelegate::CreateStatic(UAsceGameplayCueManager::DumpGameplayCues));

	static EAsceEditorLoadMode LoadMode = EAsceEditorLoadMode::LoadUpfront;
}

const bool bPreloadEvenInEditor = true;

//////////////////////////////////////////////////////////////////////

// 定义一下异步任务
struct FGameplayCueTagThreadSynchronizeGraphTask : public FAsyncGraphTaskBase
{
	TFunction<void()> TheTask;
	FGameplayCueTagThreadSynchronizeGraphTask(TFunction<void()>&& Task) : TheTask(MoveTemp(Task)) { }
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) { TheTask(); }
	ENamedThreads::Type GetDesiredThread() { return ENamedThreads::GameThread; }
};

//////////////////////////////////////////////////////////////////////

UAsceGameplayCueManager::UAsceGameplayCueManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UAsceGameplayCueManager* UAsceGameplayCueManager::Get()
{
	return Cast<UAsceGameplayCueManager>(UAbilitySystemGlobals::Get().GetGameplayCueManager());
}

void UAsceGameplayCueManager::OnCreated()
{
	Super::OnCreated();

	UpdateDelayLoadDelegateListeners();
}

//加载总是加载的cue
void UAsceGameplayCueManager::LoadAlwaysLoadedCues()
{
	if (ShouldDelayLoadGameplayCues())
	{
		//如果要延迟加载cue，先把额外的总是加载的cue加载
		UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
	
		//@TODO: Try to collect these by filtering GameplayCue. tags out of native gameplay tags?
		TArray<FName> AdditionalAlwaysLoadedCueTags;

		for (const FName& CueTagName : AdditionalAlwaysLoadedCueTags)
		{
			FGameplayTag CueTag = TagManager.RequestGameplayTag(CueTagName, /*ErrorIfNotFound=*/ false);
			if (CueTag.IsValid())
			{
				ProcessTagToPreload(CueTag, nullptr);
			}
			else
			{
				UE_LOG(LogAsce, Warning, TEXT("UAsceGameplayCueManager::AdditionalAlwaysLoadedCueTags contains invalid tag %s"), *CueTagName.ToString());
			}
		}
	}
}

//重载，返回是否异步加载运行时对象库
bool UAsceGameplayCueManager::ShouldAsyncLoadRuntimeObjectLibraries() const
{
	switch (AsceGameplayCueManagerCvars::LoadMode)
	{
	case EAsceEditorLoadMode::LoadUpfront:
		return true;
	case EAsceEditorLoadMode::PreloadAsCuesAreReferenced_GameOnly:
#if WITH_EDITOR
		if (GIsEditor)
		{
			return false;
		}
#endif
		break;
	case EAsceEditorLoadMode::PreloadAsCuesAreReferenced:
		break;
	}

	return !ShouldDelayLoadGameplayCues();
}

//是否同步加载缺失的cue，总是不同步加载，而是采用异步加载
bool UAsceGameplayCueManager::ShouldSyncLoadMissingGameplayCues() const
{
	return false;
}

bool UAsceGameplayCueManager::ShouldAsyncLoadMissingGameplayCues() const
{
	return true;
}


//打印当前加载的Cues
void UAsceGameplayCueManager::DumpGameplayCues(const TArray<FString>& Args)
{
	UAsceGameplayCueManager* GCM = Cast<UAsceGameplayCueManager>(UAbilitySystemGlobals::Get().GetGameplayCueManager());
	if (!GCM)
	{
		UE_LOG(LogAsce, Error, TEXT("DumpGameplayCues failed. No UAsceGameplayCueManager found."));
		return;
	}

	const bool bIncludeRefs = Args.Contains(TEXT("Refs"));

	UE_LOG(LogAsce, Log, TEXT("=========== Dumping Always Loaded Gameplay Cue Notifies ==========="));
	// 先打印总是加载的cue的路径
	for (UClass* CueClass : GCM->AlwaysLoadedCues)
	{
		UE_LOG(LogAsce, Log, TEXT("  %s"), *GetPathNameSafe(CueClass));
	}

	UE_LOG(LogAsce, Log, TEXT("=========== Dumping Preloaded Gameplay Cue Notifies ==========="));
	// 打印预加载的Cue
	for (UClass* CueClass : GCM->PreloadedCues)
	{
		//预加载的Cue在字典中以自己的class对象生成的ObjectKey为键，以自己的引用者集合为值，记录了所有引用者的对象
		
		TSet<FObjectKey>* ReferencerSet = GCM->PreloadedCueReferencers.Find(CueClass);
		int32 NumRefs = ReferencerSet ? ReferencerSet->Num() : 0;
		UE_LOG(LogAsce, Log, TEXT("  %s (%d refs)"), *GetPathNameSafe(CueClass), NumRefs);
		
		//如果命令附带参数Refs，就会将引用集合中的引用对象也打印出来
		if (bIncludeRefs && ReferencerSet)
		{
			for (const FObjectKey& Ref : *ReferencerSet)
			{
				UObject* RefObject = Ref.ResolveObjectPtr();
				UE_LOG(LogAsce, Log, TEXT("    ^- %s"), *GetPathNameSafe(RefObject));
			}
		}
	}

	UE_LOG(LogAsce, Log, TEXT("=========== Dumping Gameplay Cue Notifies loaded on demand ==========="));
	//打印按需加载的cue
	int32 NumMissingCuesLoaded = 0;
	if (GCM->RuntimeGameplayCueObjectLibrary.CueSet)//GameplayCueManager的运行时对象库中存放了加载的Cue对象
	{
		for (const FGameplayCueNotifyData& CueData : GCM->RuntimeGameplayCueObjectLibrary.CueSet->GameplayCueData)
		{
			if (CueData.LoadedGameplayCueClass && !GCM->AlwaysLoadedCues.Contains(CueData.LoadedGameplayCueClass) && !GCM->PreloadedCues.Contains(CueData.LoadedGameplayCueClass))
			{
				NumMissingCuesLoaded++;
				UE_LOG(LogAsce, Log, TEXT("  %s"), *CueData.LoadedGameplayCueClass->GetPathName());
			}
		}
	}

	UE_LOG(LogAsce, Log, TEXT("=========== Gameplay Cue Notify summary ==========="));
	UE_LOG(LogAsce, Log, TEXT("  ... %d cues in always loaded list"), GCM->AlwaysLoadedCues.Num());
	UE_LOG(LogAsce, Log, TEXT("  ... %d cues in preloaded list"), GCM->PreloadedCues.Num());
	UE_LOG(LogAsce, Log, TEXT("  ... %d cues loaded on demand"), NumMissingCuesLoaded);
	UE_LOG(LogAsce, Log, TEXT("  ... %d cues in total"), GCM->AlwaysLoadedCues.Num() + GCM->PreloadedCues.Num() + NumMissingCuesLoaded);
}

void UAsceGameplayCueManager::OnGameplayTagLoaded(const FGameplayTag& Tag)
{
	//当一个gameplaytag加载完成时回调
	//保证此回调函数只会同时被一个线程调用(GameplayTag加载完成的回调不是返回游戏线程调用的？)
	//这个函数可能会在其他线程被调用
	FScopeLock ScopeLock(&LoadedGameplayTagsToProcessCS);
	bool bStartTask = LoadedGameplayTagsToProcess.Num() == 0;
	/**
	*	 此时正是GameplayTag加载的时候，必然是某个对象的加载导致了gameplaytag的加载
	*    这里尝试获取当前对象序列化的上下文（SerializeContext）。
	*    在UE中，当对象被序列化（加载）时，会有一个上下文记录当前正在序列化的对象。
	*    `FUObjectThreadContext`是每个线程的UObject相关上下文，通过`GetSerializeContext()`获取序列化上下文。
	*    如果存在序列化上下文，那么我们可以获取到当前正在被序列化的对象（`SerializedObject`），这个对象通常是拥有这个Tag的对象（或者与这个Tag加载相关的对象）。
	*    如果上下文不存在，则`OwningObject`为nullptr。
	*    因此owningobject必然是引用了这个tag的对象，这可以看作这个owningobject必然是引用了这个tag对应的cue，或者这个owingobject就是这个cue自身
	*/
	FUObjectSerializeContext* LoadContext = FUObjectThreadContext::Get().GetSerializeContext();
	UObject* OwningObject = LoadContext ? LoadContext->SerializedObject : nullptr;
	LoadedGameplayTagsToProcess.Emplace(Tag, OwningObject);//加入要处理的列表，最后在非gc的时候处理加载
	if (bStartTask)
	{
		TGraphTask<FGameplayCueTagThreadSynchronizeGraphTask>::CreateTask().ConstructAndDispatchWhenReady([]()
			{
				if (GIsRunning)
				{
					if (UAsceGameplayCueManager* StrongThis = Get())
					{
						// If we are garbage collecting we cannot call StaticFindObject (or a few other static uobject functions), so we'll just wait until the GC is over and process the tags then
						if (IsGarbageCollecting())
						{
							//标记一下，将在gc后处理加载tags
							StrongThis->bProcessLoadedTagsAfterGC = true;
						}
						else
						{
							StrongThis->ProcessLoadedTags();
						}
					}
				}
			});
	}
}

void UAsceGameplayCueManager::HandlePostGarbageCollect()
{
	//如果期间发生了处理LoadedTags的请求（即GameplayTags加载完成）， 此时是gc完成发起额回调，执行处理tags并且重置标志
	if (bProcessLoadedTagsAfterGC)
	{
		ProcessLoadedTags();
	}
	bProcessLoadedTagsAfterGC = false;
}

void UAsceGameplayCueManager::ProcessLoadedTags()
{
	//处理tags加载
	TArray<FLoadedGameplayTagToProcessData> TaskLoadedGameplayTagsToProcess;
	{
		// Lock LoadedGameplayTagsToProcess just long enough to make a copy and clear
		// 这个过程可能在多个线程中异步进行，此时加一下作用域保证同时只有一个线程执行这段代码，其他线程执行到此处会等待
		// 
		FScopeLock TaskScopeLock(&LoadedGameplayTagsToProcessCS);
		TaskLoadedGameplayTagsToProcess = LoadedGameplayTagsToProcess;
		LoadedGameplayTagsToProcess.Empty();
	}

	// This might return during shutdown, and we don't want to proceed if that is the case
	if (GIsRunning)
	{
		if (RuntimeGameplayCueObjectLibrary.CueSet)
		{
			for (const FLoadedGameplayTagToProcessData& LoadedTagData : TaskLoadedGameplayTagsToProcess)
			{
				if (RuntimeGameplayCueObjectLibrary.CueSet->GameplayCueDataMap.Contains(LoadedTagData.Tag))
				{
					if (!LoadedTagData.WeakOwner.IsStale())
					{
						ProcessTagToPreload(LoadedTagData.Tag, LoadedTagData.WeakOwner.Get());
					}
				}
			}
		}
		else
		{
			UE_LOG(LogAsce, Warning, TEXT("UAsceGameplayCueManager::OnGameplayTagLoaded processed loaded tag(s) but RuntimeGameplayCueObjectLibrary.CueSet was null. Skipping processing."));
		}
	}
}

void UAsceGameplayCueManager::ProcessTagToPreload(const FGameplayTag& Tag, UObject* OwningObject)
{
	//处理预加载tag
	//只有延迟加载才需要处理，总是预加载所有cues就不用走这个函数进行处理
	switch (AsceGameplayCueManagerCvars::LoadMode)
	{
	case EAsceEditorLoadMode::LoadUpfront:
		return;
	case EAsceEditorLoadMode::PreloadAsCuesAreReferenced_GameOnly:
#if WITH_EDITOR
		if (GIsEditor)
		{
			return;
		}
#endif
		break;
	case EAsceEditorLoadMode::PreloadAsCuesAreReferenced:
		break;
	}

	check(RuntimeGameplayCueObjectLibrary.CueSet);
	
	//根据tag找到对应的cue的路径，如果已加载就直接注册，如果没有加载就加载后注册
	
	int32* DataIdx = RuntimeGameplayCueObjectLibrary.CueSet->GameplayCueDataMap.Find(Tag);
	if (DataIdx && RuntimeGameplayCueObjectLibrary.CueSet->GameplayCueData.IsValidIndex(*DataIdx))
	{
		const FGameplayCueNotifyData& CueData = RuntimeGameplayCueObjectLibrary.CueSet->GameplayCueData[*DataIdx];

		UClass* LoadedGameplayCueClass = FindObject<UClass>(nullptr, *CueData.GameplayCueNotifyObj.ToString());
		if (LoadedGameplayCueClass)
		{
			RegisterPreloadedCue(LoadedGameplayCueClass, OwningObject);
		}
		else
		{
			bool bAlwaysLoadedCue = OwningObject == nullptr;
			TWeakObjectPtr<UObject> WeakOwner = OwningObject;
			StreamableManager.RequestAsyncLoad(CueData.GameplayCueNotifyObj, FStreamableDelegate::CreateUObject(this, &ThisClass::OnPreloadCueComplete, CueData.GameplayCueNotifyObj, WeakOwner, bAlwaysLoadedCue), FStreamableManager::DefaultAsyncLoadPriority, false, false, TEXT("GameplayCueManager"));
		}
	}
}

void UAsceGameplayCueManager::OnPreloadCueComplete(FSoftObjectPath Path, TWeakObjectPtr<UObject> OwningObject, bool bAlwaysLoadedCue)
{
	if (bAlwaysLoadedCue || OwningObject.IsValid())
	{
		if (UClass* LoadedGameplayCueClass = Cast<UClass>(Path.ResolveObject()))
		{
			RegisterPreloadedCue(LoadedGameplayCueClass, OwningObject.Get());
		}
	}
}

void UAsceGameplayCueManager::RegisterPreloadedCue(UClass* LoadedGameplayCueClass, UObject* OwningObject)
{
	check(LoadedGameplayCueClass);
	//注册预加载的cue
	
	const bool bAlwaysLoadedCue = OwningObject == nullptr;
	if (bAlwaysLoadedCue)
	{
		AlwaysLoadedCues.Add(LoadedGameplayCueClass);
		PreloadedCues.Remove(LoadedGameplayCueClass);
		PreloadedCueReferencers.Remove(LoadedGameplayCueClass);
	}
	else if ((OwningObject != LoadedGameplayCueClass) && (OwningObject != LoadedGameplayCueClass->GetDefaultObject()) && !AlwaysLoadedCues.Contains(LoadedGameplayCueClass))
	{
		PreloadedCues.Add(LoadedGameplayCueClass);
		TSet<FObjectKey>& ReferencerSet = PreloadedCueReferencers.FindOrAdd(LoadedGameplayCueClass);
		ReferencerSet.Add(OwningObject);
	}
}

void UAsceGameplayCueManager::HandlePostLoadMap(UWorld* NewWorld)
{
	
	//地图加载完成时，先把预加载和总是加载的cues移除硬引用
	if (RuntimeGameplayCueObjectLibrary.CueSet)
	{
		for (UClass* CueClass : AlwaysLoadedCues)
		{
			RuntimeGameplayCueObjectLibrary.CueSet->RemoveLoadedClass(CueClass);
		}

		for (UClass* CueClass : PreloadedCues)
		{
			RuntimeGameplayCueObjectLibrary.CueSet->RemoveLoadedClass(CueClass);
		}
	}

	for (auto CueIt = PreloadedCues.CreateIterator(); CueIt; ++CueIt)
	{
		TSet<FObjectKey>& ReferencerSet = PreloadedCueReferencers.FindChecked(*CueIt);
		for (auto RefIt = ReferencerSet.CreateIterator(); RefIt; ++RefIt)
		{
			if (!RefIt->ResolveObjectPtr())
			{
				RefIt.RemoveCurrent();
			}
		}
		if (ReferencerSet.Num() == 0)
		{
			PreloadedCueReferencers.Remove(*CueIt);
			CueIt.RemoveCurrent();
		}
	}
}

void UAsceGameplayCueManager::UpdateDelayLoadDelegateListeners()
{
	//更新延迟加载cue的监听
	UGameplayTagsManager::Get().OnGameplayTagLoadedDelegate.RemoveAll(this);
	FCoreUObjectDelegates::GetPostGarbageCollect().RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

	switch (AsceGameplayCueManagerCvars::LoadMode)
	{
	case EAsceEditorLoadMode::LoadUpfront:
		//提前加载是引擎的默认流程，在编辑器加载阶段就加载就不需要延迟加载了
		return;
	case EAsceEditorLoadMode::PreloadAsCuesAreReferenced_GameOnly:
#if WITH_EDITOR
		if (GIsEditor)
		{
			return;
		}
#endif
		break;
	case EAsceEditorLoadMode::PreloadAsCuesAreReferenced:
		break;
	}

	UGameplayTagsManager::Get().OnGameplayTagLoadedDelegate.AddUObject(this, &ThisClass::OnGameplayTagLoaded);//每个gameplaytag加载完成时都会调用此回调
	FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &ThisClass::HandlePostGarbageCollect);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);
}

bool UAsceGameplayCueManager::ShouldDelayLoadGameplayCues() const
{
	//保证只在客户端加载cue
	const bool bClientDelayLoadGameplayCues = true;
	return !IsRunningDedicatedServer() && bClientDelayLoadGameplayCues;
}

const FPrimaryAssetType UFortAssetManager_GameplayCueRefsType = TEXT("GameplayCueRefs");
const FName UFortAssetManager_GameplayCueRefsName = TEXT("GameplayCueReferences");
const FName UFortAssetManager_LoadStateClient = FName(TEXT("Client"));

void UAsceGameplayCueManager::RefreshGameplayCuePrimaryAsset()
{
	//刷新GameplayCue的主资产
	TArray<FSoftObjectPath> CuePaths;
	UGameplayCueSet* RuntimeGameplayCueSet = GetRuntimeCueSet();
	if (RuntimeGameplayCueSet)
	{
		RuntimeGameplayCueSet->GetSoftObjectPaths(CuePaths);
	}

	FAssetBundleData BundleData;
	BundleData.AddBundleAssetsTruncated(UFortAssetManager_LoadStateClient, CuePaths);

	FPrimaryAssetId PrimaryAssetId = FPrimaryAssetId(UFortAssetManager_GameplayCueRefsType, UFortAssetManager_GameplayCueRefsName);
	UAssetManager::Get().AddDynamicAsset(PrimaryAssetId, FSoftObjectPath(), BundleData);
}
