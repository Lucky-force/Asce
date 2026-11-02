// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AsceMass/Combat/AsceAnimDef.h"
#include "AsceEntityModularEditor/AsceUnitModule.h"
#include "Widgets/SCompoundWidget.h"

#include "AsceUnitModuleEditorPanel.generated.h"

class IDetailsView;
template<typename OptionType> class SComboBox;
class STextBlock;
class UAsceUnitModuleConfig;
class UMassEntityConfigAsset;
class SAsceUnitModulePreviewViewport;
class UAnimSequence;
class UAsceAnimSet;
class UDataTable;
struct FAsceEntityDefRow;

/** 面板的"编辑会话"：靠 DetailsView 提供"选表 / 选配置资产"的下拉 */
UCLASS()
class UAsceUnitModuleEditorSession : public UObject
{
	GENERATED_BODY()

public:
	/** 实体定义表（`FAsceEntityDefRow`） */
	UPROPERTY(EditAnywhere, Category = "Target", meta = (DisplayName = "实体定义表"))
	TObjectPtr<UDataTable> Table = nullptr;

	/** 当前行（用下拉切换） */
	UPROPERTY(VisibleAnywhere, Category = "Target", meta = (DisplayName = "行"))
	FName RowName = NAME_None;

};

/**
 * **单位模块化编辑器**（P5a）：选表/选行 → 编辑模块（DetailsView）→ 应用（BUILD → 写 DA/def）→ 看校验与统计。
 *
 * 入口：`工具` 菜单 →「Asce：单位模块化编辑器」（`RegisterAsceUnitModuleEditorMenus()` 在模块启动时调用）。
 */
class ASCEEDITOR_API SAsceUnitModuleEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAsceUnitModuleEditor) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAsceUnitModuleEditor() override;

	/** 打开（或聚焦）独立窗口 */
	static void OpenWindow();

private:
	void OnSessionChanged();
	void RebuildRowItems();
	void LoadConfigForRow();
	void RefreshPreview();
	void RebuildClipItems();
	void PlayPreviewEntry(int32 Index);
	UAsceAnimSet* GetAnimSet() const;
	FAsceEntityDefRow* GetSelectedRow() const;

	/** 缺失时按编辑器设置目录创建 ModuleConfig / EntityTemplate，并写回行引用 */
	UAsceUnitModuleConfig* EnsureModuleConfigForRow(FAsceEntityDefRow& Row, FName RowName);
	UMassEntityConfigAsset* EnsureEntityTemplateForRow(FAsceEntityDefRow& Row, FName RowName);

	FReply OnApplyClicked();
	FReply OnSaveClicked();
	FReply OnApplyGlobalVisualizationClicked();
	FReply OnStatsClicked();
	FText GetStatsText() const;
	FText GetStatusText() const;

	TObjectPtr<UAsceUnitModuleEditorSession> Session = nullptr;
	TObjectPtr<UAsceUnitModuleConfig> Config = nullptr;

	TSharedPtr<IDetailsView> SessionDetails;
	TSharedPtr<IDetailsView> ConfigDetails;
	TSharedPtr<SAsceUnitModulePreviewViewport> PreviewViewport;
	/** 各模块汇总出来的预览产物（动画 + 几何） */
	FAsceUnitModulePreviewActions PreviewActions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ClipCombo;
	TArray<TSharedPtr<FString>> ClipItems;
	TSharedPtr<SComboBox<TSharedPtr<FName>>> RowCombo;
	TArray<TSharedPtr<FName>> RowItems;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> ReportText;

	/** 上一次"统计本表"的结果 */
	FString StatsReport;
	TSharedPtr<STextBlock> StatsText;

	/** 最近一次应用/加载的错误（显示在面板里） */
	FString LastErrors;

	/** 本次编辑会话里创建/Apply 过的资产，保存时一起落盘 */
	TSet<TWeakObjectPtr<UObject>> TouchedAssets;

	/** 重建行下拉时的重入保护（SetSelectedItem 会触发 OnSelectionChanged） */
	bool bUpdatingRowCombo = false;
};

/** 注册"工具"菜单入口（模块 StartupModule 里调用） */
void RegisterAsceUnitModuleEditorMenus();
