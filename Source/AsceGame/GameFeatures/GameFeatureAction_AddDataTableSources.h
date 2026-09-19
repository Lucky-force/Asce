// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"   // FGameFeatureStateChangeContext
#include "UObject/SoftObjectPtr.h"
#include "GameFeatureAction_AddDataTableSources.generated.h"

class UDataTable;

/** 一条"把 SDT 挂到 MDT"的配置。 */
USTRUCT()
struct FAsceDataTableSourceEntry
{
	GENERATED_BODY()

	/** 目标表：必须是 `UCompositeDataTable`（MDT），且在 `UAsceDataTableSettings` 里登记。 */
	UPROPERTY(EditAnywhere, Category = "DataTable", meta = (AssetBundles = "Server"))
	TSoftObjectPtr<UDataTable> MasterTable;

	/** 来源表（SDT）：行结构必须与 MDT 一致。 */
	UPROPERTY(EditAnywhere, Category = "DataTable", meta = (AssetBundles = "Server"))
	TSoftObjectPtr<UDataTable> SourceTable;
};

/**
 * 把若干 SDT 挂到对应 MDT 上（GameFeature 激活时挂、卸载时摘）。
 * 查询方永远只查 MDT（见 `UAsceDataTableSubsystem`）。
 *
 * 编辑器里激活也生效：子系统是 `UEngineSubsystem`（编辑器/运行时都存在），
 * 所以编辑器工具（不进 PIE）也能查到 GameFeature 提供的行。
 */
UCLASS(MinimalAPI, meta = (DisplayName = "Add Data Table Sources"))
class UGameFeatureAction_AddDataTableSources final : public UGameFeatureAction
{
	GENERATED_BODY()

public:
	//~UGameFeatureAction interface
	virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;
	//~End of UGameFeatureAction interface

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	UPROPERTY(EditAnywhere, Category = "DataTable")
	TArray<FAsceDataTableSourceEntry> Sources;

private:
	/** 每个激活上下文挂上的条目（卸载时按它摘掉）。 */
	struct FMountedEntry
	{
		TWeakObjectPtr<UDataTable> Master;
		TWeakObjectPtr<UDataTable> Source;
	};

	TMap<FGameFeatureStateChangeContext, TArray<FMountedEntry>> MountedByContext;
};
