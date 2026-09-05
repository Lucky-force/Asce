// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#pragma once

#include "GameplayCueManager.h"

#include "AsceGameplayCueManager.generated.h"

class FString;
class UClass;
class UObject;
class UWorld;
struct FObjectKey;

/**
 * UAsceGameplayCueManager
 *
 * Game-specific manager for gameplay cues
 * 
 * 专门用来管理GameplayCue的管理类
 */
UCLASS()
class UAsceGameplayCueManager : public UGameplayCueManager
{
	GENERATED_BODY()

public:
	UAsceGameplayCueManager(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	static UAsceGameplayCueManager* Get();

	//~UGameplayCueManager interface
	virtual void OnCreated() override;
	virtual bool ShouldAsyncLoadRuntimeObjectLibraries() const override;
	virtual bool ShouldSyncLoadMissingGameplayCues() const override;
	virtual bool ShouldAsyncLoadMissingGameplayCues() const override;
	//~End of UGameplayCueManager interface

	static void DumpGameplayCues(const TArray<FString>& Args);

	// When delay loading cues, this will load the cues that must be always loaded anyway
	void LoadAlwaysLoadedCues();

	// Updates the bundles for the singular gameplay cue primary asset
	void RefreshGameplayCuePrimaryAsset();

private:
	// 每当有GameplayTag被加载的时候调用
	void OnGameplayTagLoaded(const FGameplayTag& Tag);
	
	// 在GC完成时调用
	void HandlePostGarbageCollect();
	
	// 处理加载完成的Tags，将对应的cue注册
	void ProcessLoadedTags();
	
	// 处理预加载tags
	void ProcessTagToPreload(const FGameplayTag& Tag, UObject* OwningObject);
	
	// 注册cue但是tag对应的cue没有加载的时候对cue进行异步加载，此函数作为异步加载的回调在其中调用RegisterPreloadedCue
	void OnPreloadCueComplete(FSoftObjectPath Path, TWeakObjectPtr<UObject> OwningObject, bool bAlwaysLoadedCue);
	
	// 注册Cue
	void RegisterPreloadedCue(UClass* LoadedGameplayCueClass, UObject* OwningObject);
	
	// 地图加载完成时调用，清理注册的cue
	void HandlePostLoadMap(UWorld* NewWorld);
	
	// 更新延迟加载监听
	void UpdateDelayLoadDelegateListeners();
	
	bool ShouldDelayLoadGameplayCues() const;

private:
	struct FLoadedGameplayTagToProcessData
	{
		FGameplayTag Tag;
		TWeakObjectPtr<UObject> WeakOwner;

		FLoadedGameplayTagToProcessData() {}
		FLoadedGameplayTagToProcessData(const FGameplayTag& InTag, const TWeakObjectPtr<UObject>& InWeakOwner) : Tag(InTag), WeakOwner(InWeakOwner) {}
	};

private:
	// Cues that were preloaded on the client due to being referenced by content
	// 由于被引用而预先在客户端加载的Cue
	UPROPERTY(transient)
	TSet<TObjectPtr<UClass>> PreloadedCues;
	
	//缓存了这个cue对应的引用者
	TMap<FObjectKey, TSet<FObjectKey>> PreloadedCueReferencers;

	// Cues that were preloaded on the client and will always be loaded (code referenced or explicitly always loaded)
	// 始终预加载的Cues
	UPROPERTY(transient)
	TSet<TObjectPtr<UClass>> AlwaysLoadedCues;

	//临时缓存要加载的cue的tag和owingobject结构体，用来在非gc时期一并加载cue
	TArray<FLoadedGameplayTagToProcessData> LoadedGameplayTagsToProcess;
	FCriticalSection LoadedGameplayTagsToProcessCS;// 临界区锁， 使某部分代码在同一时刻只能被一个线程执行
	bool bProcessLoadedTagsAfterGC = false;
};
