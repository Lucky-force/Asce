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
class SEditableTextBox;
class STextBlock;
class UAsceUnitModuleConfig;
class UMassEntityConfigAsset;
class SAsceUnitModulePreviewViewport;
class UAnimSequence;
class UAsceAnimSet;
class UDataTable;
struct FAsceEntityDefRow;

/** 面板的"编辑会话"：当前表 + 当前行（表/行都用下拉选） */
UCLASS()
class UAsceUnitModuleEditorSession : public UObject
{
	GENERATED_BODY()

public:
	/** 实体定义表（`FAsceEntityDefRow`） */
	UPROPERTY()
	TObjectPtr<UDataTable> Table = nullptr;

	/** 当前行 */
	UPROPERTY()
	FName RowName = NAME_None;
};

/**
 * **单位模块化编辑器**：选表/选行 → 编辑模块（DetailsView）→ 应用（BUILD → 写 DA/def → 存盘）。
 *
 * - 表下拉只列 **行结构 = `FAsceEntityDefRow`** 且资产名以设置前缀（默认 `SDT`）开头的表；
 * - 新建/删除单位都在面板里完成（新建可按模板预填模块：兵种/建筑/以太建筑/空）；
 * - 缺 ModuleConfig / EntityTemplate 时按设置目录自动创建：
 *   `DA_ModuleConfig_<RowName>` / `DA_Trait_<RowName>`（已存在则直接复用）。
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
	void RefreshTableItems();
	void OnTableSelected(UDataTable* NewTable);
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

	/** 应用当前行（写模板 + def + 配置哈希），成功返回 true */
	bool ApplyModulesToRow(FAsceEntityDefRow& Row, UAsceUnitModuleConfig& InConfig);
	/** 把本次会话动过的包全部存盘，返回实际保存的资产名（用于提示） */
	TArray<FString> SaveTouchedPackages();

	/** 按模板名构造默认模块列表（兵种单位/建筑单位/以太建筑/空模板） */
	TArray<FInstancedStruct> BuildPresetModules(const FString& PresetName) const;

	FReply OnRefreshTablesClicked();
	FReply OnNewRowClicked();
	FReply OnDeleteRowClicked();
	/** 应用：写模板 DA + def 行 + 模块配置，再保存所有相关包 */
	FReply OnApplyClicked();
	FReply OnApplyGlobalVisualizationClicked();
	FReply OnStatsClicked();
	FText GetStatsText() const;
	FText GetStatusText() const;

	TObjectPtr<UAsceUnitModuleEditorSession> Session = nullptr;
	TObjectPtr<UAsceUnitModuleConfig> Config = nullptr;

	TSharedPtr<IDetailsView> ConfigDetails;
	TSharedPtr<SAsceUnitModulePreviewViewport> PreviewViewport;
	/** 各模块汇总出来的预览产物（动画 + 几何） */
	FAsceUnitModulePreviewActions PreviewActions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ClipCombo;
	TArray<TSharedPtr<FString>> ClipItems;

	/** 表下拉（只含合法的单位定义表） */
	TSharedPtr<SComboBox<UDataTable*>> TableCombo;
	TArray<UDataTable*> TableItems;

	/** 行下拉 */
	TSharedPtr<SComboBox<TSharedPtr<FName>>> RowCombo;
	TArray<TSharedPtr<FName>> RowItems;

	/** 新建：模板下拉 + 行名输入 */
	TSharedPtr<SComboBox<TSharedPtr<FString>>> PresetCombo;
	TArray<TSharedPtr<FString>> PresetItems;
	TSharedPtr<FString> SelectedPreset;
	TSharedPtr<SEditableTextBox> NewRowNameBox;
	FString NewRowNameText;

	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> ReportText;

	/** 上一次"统计本表"的结果 */
	FString StatsReport;
	TSharedPtr<STextBlock> StatsText;

	/** 最近一次应用/加载的错误（显示在面板里） */
	FString LastErrors;

	/** 本次编辑会话里创建/Apply 过的资产，保存时一起落盘 */
	TSet<TWeakObjectPtr<UObject>> TouchedAssets;

	/** 重建下拉时的重入保护（SetSelectedItem 会触发 OnSelectionChanged） */
	bool bUpdatingRowCombo = false;
	bool bUpdatingTableCombo = false;
};

/** 注册"工具"菜单入口（模块 StartupModule 里调用） */
void RegisterAsceUnitModuleEditorMenus();
