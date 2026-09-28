// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine/AssetManager.h"
#include "AsceAssetManagerStartupJob.h"
#include "Templates/SubclassOf.h"
#include "AsceAssetManager.generated.h"

#define UE_API ASCEGAME_API

class UPrimaryDataAsset;

class UAsceGameData;
class UAscePawnData;

struct FAsceBundles
{
	static const FName Equipped;
};


/**
 * UAsceAssetManager
 *
 *	Game implementation of the asset manager that overrides functionality and stores game-specific types.
 *	It is expected that most games will want to override AssetManager as it provides a good place for game-specific loading logic.
 *	This class is used by setting 'AssetManagerClassName' in DefaultEngine.ini.
 *	
 *	重载AssetManager在其中维护游戏的加载逻辑，此类在DefaultEngine.ini中修改AssetManagerClassName来应用
 *	
 */
UCLASS(MinimalAPI, Config = Game)
class UAsceAssetManager : public UAssetManager
{
	GENERATED_BODY()

public:

	UE_API UAsceAssetManager();

	// Returns the AssetManager singleton object.
	// 返回AssetManager单例
	static UE_API UAsceAssetManager& Get();

	// Returns the asset referenced by a TSoftObjectPtr.  This will synchronously load the asset if it's not already loaded.
	// 通过软对象指针返回资产对象，如果没有加载就会同步加载
	template<typename AssetType>
	static AssetType* GetAsset(const TSoftObjectPtr<AssetType>& AssetPointer, bool bKeepInMemory = true);

	// Returns the subclass referenced by a TSoftClassPtr.  This will synchronously load the asset if it's not already loaded.
	// 通过软类指针返回类对象，如果没有加载就会同步加载
	template<typename AssetType>
	static TSubclassOf<AssetType> GetSubclass(const TSoftClassPtr<AssetType>& AssetPointer, bool bKeepInMemory = true);

	// Logs all assets currently loaded and tracked by the asset manager.
	// 打印当前加载的所有被AssetManager追踪的资产
	static UE_API void DumpLoadedAssets();

	// Get GameData and PawnData currently loaded
	// 获取当前加载的GameData和PawnData
	UE_API const UAsceGameData& GetGameData(); // 实际上调用的GetOrLoadTypedGameData
	UE_API const UAscePawnData* GetDefaultPawnData() const;

protected:
	template <typename GameDataClass>
	const GameDataClass& GetOrLoadTypedGameData(const TSoftObjectPtr<GameDataClass>& DataPath)
	{
		if (TObjectPtr<UPrimaryDataAsset> const * pResult = GameDataMap.Find(GameDataClass::StaticClass()))
		{
			return *CastChecked<GameDataClass>(*pResult);
		}

		// Does a blocking load if needed
		return *CastChecked<const GameDataClass>(LoadGameDataOfClass(GameDataClass::StaticClass(), DataPath, GameDataClass::StaticClass()->GetFName()));
	}


	static UE_API UObject* SynchronousLoadAsset(const FSoftObjectPath& AssetPath);
	static UE_API bool ShouldLogAssetLoads();

	// Thread safe way of adding a loaded asset to keep in memory.
	// 将加载的资产保存引用来避免GC，此函数是线程安全的
	UE_API void AddLoadedAsset(const UObject* Asset);

	//~UAssetManager interface
	UE_API virtual void StartInitialLoading() override;
#if WITH_EDITOR
	UE_API virtual void PreBeginPIE(bool bStartSimulate) override;
#endif
	//~End of UAssetManager interface

	UE_API UPrimaryDataAsset* LoadGameDataOfClass(TSubclassOf<UPrimaryDataAsset> DataClass, const TSoftObjectPtr<UPrimaryDataAsset>& DataClassPath, FPrimaryAssetType PrimaryAssetType);

protected:

	// Global game data asset to use.
	// 要使用的全局游戏数据
	UPROPERTY(Config)
	TSoftObjectPtr<UAsceGameData> AsceGameDataPath;

	// Loaded version of the game data
	UPROPERTY(Transient)
	TMap<TObjectPtr<UClass>, TObjectPtr<UPrimaryDataAsset>> GameDataMap;

	// Pawn data used when spawning player pawns if there isn't one set on the player state.
	// PawnData 的默认值，在多个地方都有设置，如果都没有找到PawnData，将使用此默认值
	UPROPERTY(Config)
	TSoftObjectPtr<UAscePawnData> DefaultPawnData;

private:
	// Flushes the StartupJobs array. Processes all startup work.
	// 刷新StartupJobs数组，处理其中的所有同步加载
	UE_API void DoAllStartupJobs();

	// Sets up the ability system
	// 设置能力系统相关的东西
	UE_API void InitializeGameplayCueManager();

	// Called periodically during loads, could be used to feed the status to a loading screen
	// 加载的时候阶段性调用，用来反馈加载状态到加载屏幕上
	UE_API void UpdateInitialGameContentLoadPercent(float GameContentPercent);

	// The list of tasks to execute on startup. Used to track startup progress.
	// 游戏开始时要加载的任务列表
	TArray<FAsceAssetManagerStartupJob> StartupJobs;

private:
	
	// Assets loaded and tracked by the asset manager.
	// 由AssetManager加载并追踪的资产
	UPROPERTY()
	TSet<TObjectPtr<const UObject>> LoadedAssets;

	// Used for a scope lock when modifying the list of load assets.
	FCriticalSection LoadedAssetsCritical;
};


template<typename AssetType>
AssetType* UAsceAssetManager::GetAsset(const TSoftObjectPtr<AssetType>& AssetPointer, bool bKeepInMemory)
{
	AssetType* LoadedAsset = nullptr;

	const FSoftObjectPath& AssetPath = AssetPointer.ToSoftObjectPath();

	if (AssetPath.IsValid())
	{
		LoadedAsset = AssetPointer.Get();
		if (!LoadedAsset)
		{
			LoadedAsset = Cast<AssetType>(SynchronousLoadAsset(AssetPath));
			ensureAlwaysMsgf(LoadedAsset, TEXT("Failed to load asset [%s]"), *AssetPointer.ToString());
		}

		if (LoadedAsset && bKeepInMemory)
		{
			// Added to loaded asset list.
			Get().AddLoadedAsset(Cast<UObject>(LoadedAsset));
		}
	}

	return LoadedAsset;
}

template<typename AssetType>
TSubclassOf<AssetType> UAsceAssetManager::GetSubclass(const TSoftClassPtr<AssetType>& AssetPointer, bool bKeepInMemory)
{
	TSubclassOf<AssetType> LoadedSubclass;

	const FSoftObjectPath& AssetPath = AssetPointer.ToSoftObjectPath();

	if (AssetPath.IsValid())
	{
		LoadedSubclass = AssetPointer.Get();
		if (!LoadedSubclass)
		{
			LoadedSubclass = Cast<UClass>(SynchronousLoadAsset(AssetPath));
			ensureAlwaysMsgf(LoadedSubclass, TEXT("Failed to load asset class [%s]"), *AssetPointer.ToString());
		}

		if (LoadedSubclass && bKeepInMemory)
		{
			// Added to loaded asset list.
			Get().AddLoadedAsset(Cast<UObject>(LoadedSubclass));
		}
	}

	return LoadedSubclass;
}

#undef UE_API
