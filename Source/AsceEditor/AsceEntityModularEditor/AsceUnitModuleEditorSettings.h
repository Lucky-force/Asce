// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPath.h"

#include "AsceUnitModuleEditorSettings.generated.h"

/**
 * 单位模块化编辑器的默认资产目录。
 *
 * EntityDef 行里的 ModuleConfig / EntityTemplate 缺失时，编辑器会自动在下面两个目录创建：
 * - ModuleConfig：UAsceUnitModuleConfig
 * - EntityTemplate：UMassEntityConfigAsset
 */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "Asce Unit Module Editor"))
class ASCEEDITOR_API UAsceUnitModuleEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAsceUnitModuleEditorSettings();

	UPROPERTY(EditAnywhere, config, Category = "Directories", meta = (RelativeToGameContentDir))
	FDirectoryPath ModuleConfigDirectory;

	UPROPERTY(EditAnywhere, config, Category = "Directories", meta = (RelativeToGameContentDir))
	FDirectoryPath EntityTemplateDirectory;

	/** 表下拉只列**资产名以这个前缀开头**的 DataTable（默认 `SDT`）；留空 = 不按名字过滤 */
	UPROPERTY(EditAnywhere, config, Category = "DataTable")
	FString DataTablePrefix = TEXT("SDT");

	virtual FName GetCategoryName() const override { return FName(TEXT("Asce")); }
};
