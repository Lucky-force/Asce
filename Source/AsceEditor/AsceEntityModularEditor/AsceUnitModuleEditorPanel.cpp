// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/AsceUnitModuleEditorPanel.h"

#include "Engine/DataTable.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/DataAssetFactory.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySettings.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "Templates/GuardValueAccessors.h"
#include "PropertyEditorModule.h"
#include "Styling/CoreStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include "AsceMass/Combat/AsceAnimDef.h"
#include "AsceMass/Data/AsceEntityDefTypes.h"
#include "AsceMass/Data/AsceUnitDef.h"
#include "AsceMass/Traits/AsceRTSEntityTrait.h"

#include "AsceEntityModularEditor/AsceUnitModuleConfig.h"
#include "AsceEntityModularEditor/AsceUnitModuleEditorSettings.h"
#include "AsceEntityModularEditor/AsceUnitModuleGenerator.h"
#include "AsceEntityModularEditor/AsceUnitModulePreviewViewport.h"
#include "AsceEntityModularEditor/AsceUnitModuleRegistry.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleAnimation.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleCombat.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleMovement.h"

#define LOCTEXT_NAMESPACE "AsceUnitModuleEditor"

void SAsceUnitModuleEditor::Construct(const FArguments& InArgs)
{
	Session = NewObject<UAsceUnitModuleEditorSession>(GetTransientPackage());
	Session->AddToRoot();

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs ConfigArgs;
	ConfigArgs.bShowOptions = false;
	ConfigArgs.bAllowSearch = true;
	ConfigArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	ConfigDetails = PropertyEditor.CreateDetailView(ConfigArgs);
	ConfigDetails->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&)
	{
		RefreshPreview();
	});

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(6.f)
		[
			SNew(SVerticalBox)
			// 顶部：表 / 行 / 新建删除
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(SVerticalBox)
				// 表：只列"行结构 = FAsceEntityDefRow 且名字以前缀开头"的表
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 4.f, 0.f)
					[
						SNew(STextBlock).Text(LOCTEXT("TableLabel", "表："))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SAssignNew(TableCombo, SComboBox<UDataTable*>)
						.OptionsSource(&TableItems)
						.OnGenerateWidget_Lambda([](UDataTable* Item)
						{
							return SNew(STextBlock).Text(Item ? FText::FromString(Item->GetName()) : FText::GetEmpty());
						})
						.OnSelectionChanged_Lambda([this](UDataTable* Item, ESelectInfo::Type)
						{
							if (bUpdatingTableCombo)
							{
								return;
							}
							OnTableSelected(Item);
						})
						[
							SNew(STextBlock).Text_Lambda([this]()
							{
								return Session && Session->Table
									? FText::FromString(Session->Table->GetName())
									: LOCTEXT("NoTable", "（未选择实体定义表）");
							})
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("RefreshTables", "刷新"))
						.OnClicked(this, &SAsceUnitModuleEditor::OnRefreshTablesClicked)
					]
				]
				// 行 + 新建（模板/名字） + 删除
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 4.f, 0.f)
					[
						SNew(STextBlock).Text(LOCTEXT("RowLabel", "行："))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SAssignNew(RowCombo, SComboBox<TSharedPtr<FName>>)
						.OptionsSource(&RowItems)
						.OnGenerateWidget_Lambda([](TSharedPtr<FName> Item)
						{
							return SNew(STextBlock).Text(FText::FromName(Item.IsValid() ? *Item : NAME_None));
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<FName> Item, ESelectInfo::Type)
						{
							if (bUpdatingRowCombo || !Item.IsValid())
							{
								return;
							}
							Session->RowName = *Item;
							OnSessionChanged();
						})
						[
							SNew(STextBlock).Text_Lambda([this]()
							{
								return Session && !Session->RowName.IsNone()
									? FText::FromName(Session->RowName)
									: LOCTEXT("NoRow", "（未选择行）");
							})
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.f, 0.f, 4.f, 0.f)
					[
						SNew(STextBlock).Text(LOCTEXT("NewRowLabel", "新建："))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SAssignNew(PresetCombo, SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&PresetItems)
						.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
						{
							return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString()));
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
						{
							if (Item.IsValid())
							{
								SelectedPreset = Item;
							}
						})
						[
							SNew(STextBlock).Text_Lambda([this]()
							{
								return SelectedPreset.IsValid()
									? FText::FromString(*SelectedPreset)
									: LOCTEXT("NoPreset", "（模板）");
							})
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.f, 0.f, 0.f, 0.f)
					[
						SAssignNew(NewRowNameBox, SEditableTextBox)
						.HintText(LOCTEXT("NewRowHint", "新单位 ID"))
						.MinDesiredWidth(110.f)
						.OnTextChanged_Lambda([this](const FText& Text)
						{
							NewRowNameText = Text.ToString();
						})
						.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
						{
							NewRowNameText = Text.ToString();
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("NewRow", "新建单位"))
						.OnClicked(this, &SAsceUnitModuleEditor::OnNewRowClicked)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("DeleteRow", "删除单位"))
						.OnClicked(this, &SAsceUnitModuleEditor::OnDeleteRowClicked)
					]
				]
			]
			// 主区：左预览 / 右配置 outline（可拖分隔条）
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding(0.f, 4.f, 0.f, 0.f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)
				+ SSplitter::Slot()
				.Value(0.46f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 0.f, 4.f, 4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("ClipLabel", "预览动画："))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						[
							SAssignNew(ClipCombo, SComboBox<TSharedPtr<FString>>)
							.OptionsSource(&ClipItems)
							.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
							{
								return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString()));
							})
							.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
							{
								if (!Item.IsValid())
								{
									return;
								}
								PlayPreviewEntry(ClipItems.IndexOfByKey(Item));
							})
							[
								SNew(STextBlock).Text_Lambda([this]()
								{
									return ClipCombo.IsValid() && ClipCombo->GetSelectedItem().IsValid()
										? FText::FromString(*ClipCombo->GetSelectedItem())
										: FText::GetEmpty();
								})
							]
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					[
						SAssignNew(PreviewViewport, SAsceUnitModulePreviewViewport)
					]
				]
				+ SSplitter::Slot()
				.Value(0.54f)
				[
					SNew(SVerticalBox)
					// 右侧：模块配置 outline（滚动）
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					.Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							ConfigDetails.ToSharedRef()
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.f, 4.f, 0.f, 0.f)
					[
						SAssignNew(StatsText, STextBlock)
						.AutoWrapText(true)
						.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)))
						.Text(this, &SAsceUnitModuleEditor::GetStatsText)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.f, 2.f, 0.f, 0.f)
					[
						SAssignNew(StatusText, STextBlock)
						.AutoWrapText(true)
						.ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.45f, 0.35f)))
						.Text(this, &SAsceUnitModuleEditor::GetStatusText)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.f, 2.f, 0.f, 0.f)
					[
						SAssignNew(ReportText, STextBlock)
						.AutoWrapText(true)
						.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 1.f)))
						.Text_Lambda([this]() { return FText::FromString(StatsReport); })
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.f, 6.f, 0.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.f, 0.f, 6.f, 0.f)
						[
							SNew(SButton)
							.Text(LOCTEXT("Apply", "应用（写 DA + 表 + 存盘）"))
							.OnClicked(this, &SAsceUnitModuleEditor::OnApplyClicked)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.f, 0.f, 6.f, 0.f)
						[
							SNew(SButton)
							.Text(LOCTEXT("ApplyGlobalVisual", "全局 LOD 应用到所有单位模板"))
							.OnClicked(this, &SAsceUnitModuleEditor::OnApplyGlobalVisualizationClicked)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(LOCTEXT("Stats", "统计全表 archetype 组合"))
							.OnClicked(this, &SAsceUnitModuleEditor::OnStatsClicked)
						]
					]
				]
			]
		]
	];

	PresetItems.Add(MakeShared<FString>(TEXT("兵种单位")));
	PresetItems.Add(MakeShared<FString>(TEXT("建筑单位")));
	PresetItems.Add(MakeShared<FString>(TEXT("以太建筑")));
	PresetItems.Add(MakeShared<FString>(TEXT("空模板")));
	SelectedPreset = PresetItems[0];
	if (PresetCombo.IsValid())
	{
		PresetCombo->SetSelectedItem(SelectedPreset);
	}

	RefreshTableItems();
	RebuildRowItems();
	LoadConfigForRow();
}

SAsceUnitModuleEditor::~SAsceUnitModuleEditor()
{
	if (Session)
	{
		Session->RemoveFromRoot();
	}
	if (Config)
	{
		Config->RemoveFromRoot();
	}
}

FAsceEntityDefRow* SAsceUnitModuleEditor::GetSelectedRow() const
{
	if (!Session || !Session->Table || Session->RowName.IsNone())
	{
		return nullptr;
	}
	return Session->Table->FindRow<FAsceEntityDefRow>(Session->RowName, TEXT("AsceUnitModuleEditor"), /*bWarnIfRowMissing*/ false);
}

void SAsceUnitModuleEditor::OnSessionChanged()
{
	RebuildRowItems();
	LoadConfigForRow();
}

void SAsceUnitModuleEditor::RefreshTableItems()
{
	const UAsceUnitModuleEditorSettings* Settings = GetDefault<UAsceUnitModuleEditorSettings>();
	const FString Prefix = Settings ? Settings->DataTablePrefix : FString(TEXT("SDT"));

	TableItems.Reset();
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UDataTable::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);
	for (const FAssetData& Asset : Assets)
	{
		if (!Prefix.IsEmpty() && !Asset.AssetName.ToString().StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			continue;
		}
		// 只有行结构是 FAsceEntityDefRow 的表才允许选（其它表结构在这个面板里没有意义）
		UDataTable* Table = Cast<UDataTable>(Asset.GetAsset());
		if (!Table || Table->GetRowStruct() != FAsceEntityDefRow::StaticStruct())
		{
			continue;
		}
		TableItems.Add(Table);
	}

	TableItems.Sort([](const UDataTable& A, const UDataTable& B)
	{
		return A.GetName() < B.GetName();
	});

	if (TableCombo.IsValid())
	{
		TableCombo->RefreshOptions();
	}

	// 当前表不在列表里（被过滤掉 / 还没选）⇒ 清空
	if (Session && Session->Table && !TableItems.Contains(Session->Table))
	{
		Session->Table = nullptr;
		Session->RowName = NAME_None;
	}
	if (!Session || !Session->Table)
	{
		if (!TableItems.IsEmpty())
		{
			Session->Table = TableItems[0];
			if (TableCombo.IsValid())
			{
				TGuardValue<bool> Guard(bUpdatingTableCombo, true);
				TableCombo->SetSelectedItem(TableItems[0]);
			}
		}
		else if (TableCombo.IsValid())
		{
			TGuardValue<bool> Guard(bUpdatingTableCombo, true);
			TableCombo->ClearSelection();
		}
	}
}

void SAsceUnitModuleEditor::OnTableSelected(UDataTable* NewTable)
{
	if (!Session)
	{
		return;
	}
	Session->Table = NewTable;
	Session->RowName = NAME_None;
	RebuildRowItems();
	LoadConfigForRow();
}


void SAsceUnitModuleEditor::RebuildRowItems()
{
	const FName Previous = Session ? Session->RowName : NAME_None;

	RowItems.Reset();
	if (Session && Session->Table)
	{
		TArray<FName> Names;
		Session->Table->GetRowMap().GetKeys(Names);
		Names.Sort(FNameLexicalLess());
		for (const FName& Name : Names)
		{
			RowItems.Add(MakeShared<FName>(Name));
		}
	}
	if (RowCombo.IsValid())
	{
		RowCombo->RefreshOptions();
	}

	// 尽量保留已选行
	if (Session && !Previous.IsNone())
	{
		for (const TSharedPtr<FName>& Item : RowItems)
		{
			if (Item.IsValid() && *Item == Previous)
			{
				TGuardValue<bool> Guard(bUpdatingRowCombo, true);
				RowCombo->SetSelectedItem(Item);
				return;
			}
		}
	}
	if (RowCombo.IsValid() && RowItems.Num() > 0)
	{
		TGuardValue<bool> Guard(bUpdatingRowCombo, true);
		RowCombo->SetSelectedItem(RowItems[0]);
		if (Session)
		{
			Session->RowName = *RowItems[0];
		}
	}
}

void SAsceUnitModuleEditor::LoadConfigForRow()
{
	if (Config)
	{
		Config->RemoveFromRoot();
		Config = nullptr;
	}
	LastErrors.Reset();

	FAsceEntityDefRow* Row = GetSelectedRow();
	if (!Row || !Row->Def.GetPtr<FAsceRTSUnitDef>())
	{
		if (ConfigDetails.IsValid())
		{
			ConfigDetails->SetObject(nullptr);
		}
		RefreshPreview();
		return;
	}

	EnsureEntityTemplateForRow(*Row, Session->RowName);
	Config = UAsceUnitModuleConfig::LoadForDef(*Row);
	if (!Config)
	{
		Config = EnsureModuleConfigForRow(*Row, Session->RowName);
	}
	if (Config)
	{
		Config->AddToRoot();
	}

	if (ConfigDetails.IsValid())
	{
		ConfigDetails->SetObject(Config.Get());
	}
	RefreshPreview();
}

UAsceUnitModuleConfig* SAsceUnitModuleEditor::EnsureModuleConfigForRow(FAsceEntityDefRow& Row, const FName RowName)
{
	const UAsceUnitModuleEditorSettings* Settings = GetDefault<UAsceUnitModuleEditorSettings>();
	FString Directory = Settings ? Settings->ModuleConfigDirectory.Path : FString();
	if (Directory.IsEmpty())
	{
		Directory = TEXT("/Game/DataTables/UnitModules");
	}

	// 约定的固定资产名；已存在就直接复用（不再产生 _1/_2 后缀）
	const FString AssetName = FString::Printf(TEXT("DA_ModuleConfig_%s"), *RowName.ToString());
	const FString PackagePath = FPaths::Combine(Directory, AssetName);

	UAsceUnitModuleConfig* NewConfig = LoadObject<UAsceUnitModuleConfig>(nullptr, *PackagePath);
	if (!NewConfig)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
		Factory->DataAssetClass = UAsceUnitModuleConfig::StaticClass();
		NewConfig = Cast<UAsceUnitModuleConfig>(AssetTools.CreateAsset(AssetName, Directory, UAsceUnitModuleConfig::StaticClass(), Factory));
	}
	if (!NewConfig)
	{
		LastErrors += FString::Printf(TEXT("无法创建 ModuleConfig：%s\n"), *PackagePath);
		return nullptr;
	}

#if WITH_EDITORONLY_DATA
	Row.ModuleConfig = FSoftObjectPath(NewConfig);
#else
	(void)Row;
#endif
	TouchedAssets.Add(NewConfig);
	if (Session && Session->Table)
	{
		Session->Table->MarkPackageDirty();
	}
	return NewConfig;
}

UMassEntityConfigAsset* SAsceUnitModuleEditor::EnsureEntityTemplateForRow(FAsceEntityDefRow& Row, const FName RowName)
{
	if (UMassEntityConfigAsset* Existing = Row.EntityTemplate.LoadSynchronous())
	{
		TArray<FText> Errors;
		AsceUnitModuleGenerator::EnsureDefaultTemplateTraits(Existing, RowName, Errors);
		for (const FText& Error : Errors)
		{
			LastErrors += Error.ToString() + TEXT("\n");
		}
		TouchedAssets.Add(Existing);
		return Existing;
	}

	const UAsceUnitModuleEditorSettings* Settings = GetDefault<UAsceUnitModuleEditorSettings>();
	FString Directory = Settings ? Settings->EntityTemplateDirectory.Path : FString();
	if (Directory.IsEmpty())
	{
		Directory = TEXT("/Game/DataTables/UnitTemplates");
	}

	// 约定的固定资产名；已存在就直接复用
	const FString AssetName = FString::Printf(TEXT("DA_Trait_%s"), *RowName.ToString());
	const FString PackagePath = FPaths::Combine(Directory, AssetName);

	UMassEntityConfigAsset* Template = LoadObject<UMassEntityConfigAsset>(nullptr, *PackagePath);
	if (!Template)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
		Factory->DataAssetClass = UMassEntityConfigAsset::StaticClass();
		Template = Cast<UMassEntityConfigAsset>(AssetTools.CreateAsset(AssetName, Directory, UMassEntityConfigAsset::StaticClass(), Factory));
	}
	if (!Template)
	{
		LastErrors += FString::Printf(TEXT("无法创建 EntityTemplate：%s\n"), *PackagePath);
		return nullptr;
	}

	Template->Modify();
	if (UAsceRTSEntityTrait* EntityTrait = Cast<UAsceRTSEntityTrait>(Template->AddTrait(UAsceRTSEntityTrait::StaticClass())))
	{
		EntityTrait->UniqueType.RowName = RowName;
	}

	TArray<FText> Errors;
	AsceUnitModuleGenerator::EnsureDefaultTemplateTraits(Template, RowName, Errors);
	for (const FText& Error : Errors)
	{
		LastErrors += Error.ToString() + TEXT("\n");
	}

	Row.EntityTemplate = Template;
	TouchedAssets.Add(Template);
	if (Session && Session->Table)
	{
		Session->Table->MarkPackageDirty();
	}
	return Template;
}

void SAsceUnitModuleEditor::RefreshPreview()
{
	// 汇总：每个模块自己决定要预览什么
	PreviewActions = FAsceUnitModulePreviewActions();
	if (Config)
	{
		for (const FInstancedStruct& Module : Config->Modules)
		{
			if (const FAsceUnitModule* Base = Module.GetPtr<FAsceUnitModule>())
			{
				Base->CollectPreview(PreviewActions);
			}
		}
	}

	if (PreviewViewport.IsValid())
	{
		PreviewViewport->ApplyPreview(PreviewActions);
	}
	RebuildClipItems();
}

UAsceAnimSet* SAsceUnitModuleEditor::GetAnimSet() const
{
	if (Config)
	{
		for (const FInstancedStruct& Module : Config->Modules)
		{
			if (const FAsceUnitModuleAnimation* Animation = Module.GetPtr<FAsceUnitModuleAnimation>())
			{
				return Animation->AnimSet.LoadSynchronous();
			}
		}
	}
	return nullptr;
}

void SAsceUnitModuleEditor::RebuildClipItems()
{
	ClipItems.Reset();

	// 没有模块动画时，退回"AnimSet 里的全部 clip"（调试用）
	if (PreviewActions.Animations.Num() == 0)
	{
		if (UAsceAnimSet* AnimSet = GetAnimSet())
		{
			TArray<TPair<FString, TSoftObjectPtr<UAnimSequence>>> All;
			for (const TPair<TSoftObjectPtr<UAnimSequence>, FAsceAnimClip>& Pair : AnimSet->Clips)
			{
				All.Emplace(Pair.Key.GetAssetName(), Pair.Key);
			}
			All.Sort([](const TPair<FString, TSoftObjectPtr<UAnimSequence>>& A, const TPair<FString, TSoftObjectPtr<UAnimSequence>>& B)
			{
				return A.Key < B.Key;
			});
			for (const TPair<FString, TSoftObjectPtr<UAnimSequence>>& Entry : All)
			{
				PreviewActions.AddAnimation(Entry.Key, Entry.Value);
			}
		}
	}

	for (const FAsceUnitModulePreviewActions::FAnimationEntry& Entry : PreviewActions.Animations)
	{
		ClipItems.Add(MakeShared<FString>(Entry.Label));
	}

	if (ClipCombo.IsValid())
	{
		ClipCombo->RefreshOptions();
		if (ClipItems.Num() > 0)
		{
			ClipCombo->SetSelectedItem(ClipItems[0]);
			PlayPreviewEntry(0);
		}
	}
}

void SAsceUnitModuleEditor::PlayPreviewEntry(const int32 Index)
{
	UAsceAnimSet* AnimSet = GetAnimSet();
	if (!AnimSet || !PreviewActions.Animations.IsValidIndex(Index) || !PreviewViewport.IsValid())
	{
		return;
	}

	const TSoftObjectPtr<UAnimSequence>& Sequence = PreviewActions.Animations[Index].Sequence;
	if (const FAsceAnimClip* Clip = AnimSet->Clips.Find(Sequence))
	{
		PreviewViewport->SetAnim(AnimSet, *Clip);
		LastErrors.Reset();
	}
	else
	{
		LastErrors = FString::Printf(TEXT("动画 [%s] 不在 AnimSet 的 Clips 里（需要先烘焙）"), *Sequence.GetAssetName());
	}
}

FText SAsceUnitModuleEditor::GetStatsText() const
{
	if (!Config)
	{
		return LOCTEXT("NoConfig", "（没有模块配置：给这一行指定一个模块配置资产）");
	}

	const FAsceUnitModuleBuildResult Result = AsceUnitModuleGenerator::Build(Config->Modules);

	int32 TotalBytes = 0;
	FString Fragments;
	for (const FInstancedStruct& Fragment : Result.Fragments)
	{
		const UScriptStruct* Type = Fragment.GetScriptStruct();
		if (!Type)
		{
			continue;
		}
		const int32 Size = Type->GetStructureSize();
		TotalBytes += Size;
		Fragments += FString::Printf(TEXT("%s(%dB) "), *Type->GetName(), Size);
	}

	FString StateText;
	if (!Config->HasBeenApplied())
	{
		StateText = TEXT("【首次未应用：点应用会写入 DA 和表】");
	}
	else if (Config->IsDirty())
	{
		StateText = TEXT("【模块已改，待应用】");
	}
	else
	{
		StateText = TEXT("已应用");
	}
	return FText::FromString(FString::Printf(TEXT("%s   fragment %d 个 ≈ %dB/实体\n%s"),
		*StateText, Result.Fragments.Num(), TotalBytes, *Fragments));
}

FText SAsceUnitModuleEditor::GetStatusText() const
{
	return FText::FromString(LastErrors);
}

bool SAsceUnitModuleEditor::ApplyModulesToRow(FAsceEntityDefRow& Row, UAsceUnitModuleConfig& InConfig)
{
#if WITH_EDITORONLY_DATA
	// 行里没配时，把当前配置写回行（editor-only 字段）
	if (Row.ModuleConfig.IsNull())
	{
		Row.ModuleConfig = FSoftObjectPath(&InConfig);
	}
#endif

	TArray<FText> Errors;
	if (!InConfig.ApplyToDef(Row, Errors))
	{
		for (const FText& Error : Errors)
		{
			LastErrors += Error.ToString() + TEXT("\n");
		}
		return false;
	}

	TouchedAssets.Add(&InConfig);
	if (UMassEntityConfigAsset* Template = Row.EntityTemplate.LoadSynchronous())
	{
		TouchedAssets.Add(Template);
	}
	if (Session && Session->Table)
	{
		Session->Table->HandleDataTableChanged(Session->RowName);
		Session->Table->MarkPackageDirty();   // def 字段写了：血量/体积/视野/动画/技能槽
	}
	return true;
}

TArray<FString> SAsceUnitModuleEditor::SaveTouchedPackages()
{
	TSet<UPackage*> Packages;
	auto AddPackage = [&Packages](UObject* Object)
	{
		if (Object && Object->GetOutermost())
		{
			Packages.Add(Object->GetOutermost());
		}
	};

	AddPackage(Config.Get());
	AddPackage(Session ? Session->Table.Get() : nullptr);
	if (const FAsceEntityDefRow* Row = GetSelectedRow())
	{
		AddPackage(Row->EntityTemplate.LoadSynchronous());
	}
	for (const TWeakObjectPtr<UObject>& Touched : TouchedAssets)
	{
		AddPackage(Touched.Get());
	}

	TArray<UPackage*> PackagesToSave;
	TArray<FString> SavedNames;
	for (UPackage* Package : Packages)
	{
		if (!Package)
		{
			continue;
		}
		Package->MarkPackageDirty();
		PackagesToSave.Add(Package);
		SavedNames.Add(FPackageName::GetShortName(Package->GetName()));
	}
	if (PackagesToSave.Num() == 0)
	{
		return SavedNames;
	}
	if (!FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, /*bCheckDirty*/ false, /*bPromptToSave*/ false))
	{
		return TArray<FString>();
	}
	TouchedAssets.Reset();
	return SavedNames;
}

TArray<FInstancedStruct> SAsceUnitModuleEditor::BuildPresetModules(const FString& PresetName) const
{
	TArray<FName> ModuleIds;
	if (PresetName == TEXT("兵种单位"))
	{
		ModuleIds = { TEXT("Health"), TEXT("Volume"), TEXT("Movement"), TEXT("Vision")
			, TEXT("ActionState"), TEXT("Animation"), TEXT("Visualization"), TEXT("Combat"), TEXT("Skills") };
	}
	else if (PresetName == TEXT("建筑单位"))
	{
		ModuleIds = { TEXT("Health"), TEXT("Volume"), TEXT("Building"), TEXT("Visualization") };
	}
	else if (PresetName == TEXT("以太建筑"))
	{
		ModuleIds = { TEXT("Health"), TEXT("Volume"), TEXT("Building"), TEXT("Ether"), TEXT("Visualization") };
	}
	// "空模板"：不预填

	TArray<FInstancedStruct> Modules;
	Modules.Reserve(ModuleIds.Num());
	for (const FName ModuleId : ModuleIds)
	{
		FInstancedStruct Module = AsceUnitModuleRegistry::MakeDefaultModule(ModuleId);
		if (Module.IsValid())
		{
			Modules.Add(MoveTemp(Module));
		}
	}
	return Modules;
}

FReply SAsceUnitModuleEditor::OnRefreshTablesClicked()
{
	LastErrors.Reset();
	RefreshTableItems();
	RebuildRowItems();
	LoadConfigForRow();
	return FReply::Handled();
}

FReply SAsceUnitModuleEditor::OnApplyClicked()
{
	LastErrors.Reset();

	FAsceEntityDefRow* Row = GetSelectedRow();
	if (!Row)
	{
		LastErrors = TEXT("没有选中行");
	}
	else if (!Config)
	{
		LastErrors = TEXT("没有模块配置资产");
	}
	else
	{
		const FScopedTransaction Transaction(LOCTEXT("ApplyUnitModules", "Apply Unit Modules"));
		if (Session && Session->Table)
		{
			Session->Table->Modify();
		}
		Config->Modify();

		if (ApplyModulesToRow(*Row, *Config))
		{
			const TArray<FString> Saved = SaveTouchedPackages();
			FString Message = FString::Printf(TEXT("已应用单位 %s。"), *Session->RowName.ToString());
			if (Saved.Num() > 0)
			{
				Message += FString::Printf(TEXT("\n已保存 %d 个资产：%s"), Saved.Num(), *FString::Join(Saved, TEXT("，")));
			}
			else
			{
				Message += TEXT("\n（保存被取消或没有任何需要保存的包）");
			}
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message), LOCTEXT("ApplyDoneTitle", "应用完成"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[AsceUnitModule] Apply failed:\n%s"), *LastErrors);
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(LastErrors), LOCTEXT("ApplyFailedTitle", "应用模块失败"));
		}
	}

	RefreshPreview();
	return FReply::Handled();
}

FReply SAsceUnitModuleEditor::OnApplyGlobalVisualizationClicked()
{
	if (!Session || !Session->Table)
	{
		LastErrors = TEXT("没有选择实体定义表");
		return FReply::Handled();
	}

	const TArray<FName> RowNames = Session->Table->GetRowNames();
	for (const FName RowName : RowNames)
	{
		FAsceEntityDefRow* Row = Session->Table->FindRow<FAsceEntityDefRow>(RowName, TEXT("ApplyGlobalVisualization"), false);
		if (!Row || !Row->Def.GetPtr<FAsceRTSUnitDef>())
		{
			continue;
		}

		if (UMassEntityConfigAsset* Template = EnsureEntityTemplateForRow(*Row, RowName))
		{
			TArray<FText> Errors;
			AsceUnitModuleGenerator::EnsureDefaultTemplateTraits(Template, RowName, Errors);
			for (const FText& Error : Errors)
			{
				LastErrors += Error.ToString() + TEXT("\n");
			}
			TouchedAssets.Add(Template);
		}
	}

	Session->Table->MarkPackageDirty();
	Session->Table->HandleDataTableChanged();
	LastErrors = LastErrors.IsEmpty() ? TEXT("已将全局 LOD 应用到所有单位模板（保存后生效）") : LastErrors;
	return FReply::Handled();
}

FReply SAsceUnitModuleEditor::OnNewRowClicked()
{
	LastErrors.Reset();

	if (!Session || !Session->Table)
	{
		LastErrors = TEXT("请先选择一个实体定义表（行结构 FAsceEntityDefRow、名字以 SDT 开头）");
		return FReply::Handled();
	}

	const FString NameText = NewRowNameText.TrimStartAndEnd();
	if (NameText.IsEmpty())
	{
		LastErrors = TEXT("请输入新单位的行名（ID）");
		return FReply::Handled();
	}
	const FName NewName(*NameText);
	if (Session->Table->GetRowMap().Contains(NewName))
	{
		LastErrors = FString::Printf(TEXT("行 [%s] 已存在"), *NameText);
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("NewUnitRow", "New Unit Row"));
	Session->Table->Modify();

	FAsceEntityDefRow NewRow;
	NewRow.Def = FInstancedStruct::Make<FAsceRTSUnitDef>();
	NewRow.DisplayName = FText::FromString(NameText);
	Session->Table->AddRow(NewName, NewRow);
	FAsceEntityDefRow* Row = Session->Table->FindRow<FAsceEntityDefRow>(NewName, TEXT("NewUnitRow"), /*bWarnIfRowMissing*/ false);
	if (!Row)
	{
		LastErrors = FString::Printf(TEXT("添加行 [%s] 失败"), *NameText);
		return FReply::Handled();
	}

	// 缺什么建什么：DA_Trait_<Row> / DA_ModuleConfig_<Row>
	EnsureEntityTemplateForRow(*Row, NewName);
	if (UAsceUnitModuleConfig* NewConfig = EnsureModuleConfigForRow(*Row, NewName))
	{
		const FString PresetName = SelectedPreset.IsValid() ? *SelectedPreset : FString();
		NewConfig->Modules = BuildPresetModules(PresetName);
		NewConfig->MarkPackageDirty();
	}

	// 选中新行并立刻应用一次（让模板/def 与模板预设一致）
	Session->RowName = NewName;
	RebuildRowItems();
	LoadConfigForRow();

	FString Message = FString::Printf(TEXT("已创建单位 %s（模板：%s）。"),
		*NameText, *NewName.ToString());

	bool bApplied = false;
	if (FAsceEntityDefRow* SelectedRow = GetSelectedRow())
	{
		if (Config)
		{
			bApplied = ApplyModulesToRow(*SelectedRow, *Config);
		}
	}
	if (bApplied)
	{
		const TArray<FString> Saved = SaveTouchedPackages();
		if (Saved.Num() > 0)
		{
			Message += FString::Printf(TEXT("\n已保存 %d 个资产：%s"), Saved.Num(), *FString::Join(Saved, TEXT("，")));
		}
	}
	else if (!LastErrors.IsEmpty())
	{
		Message += TEXT("\n") + LastErrors;
	}

	NewRowNameText.Reset();
	if (NewRowNameBox.IsValid())
	{
		NewRowNameBox->SetText(FText::GetEmpty());
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message), LOCTEXT("NewUnitDoneTitle", "新建单位"));
	RefreshPreview();
	return FReply::Handled();
}

FReply SAsceUnitModuleEditor::OnDeleteRowClicked()
{
	LastErrors.Reset();

	if (!Session || !Session->Table || Session->RowName.IsNone())
	{
		LastErrors = TEXT("没有选中要删除的行");
		return FReply::Handled();
	}

	const FName RowName = Session->RowName;
	const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo,
		FText::FromString(FString::Printf(TEXT("删除单位 [%s]？\n会同时删除它的 DA_Trait_ / DA_ModuleConfig_ 资产。"), *RowName.ToString())),
		LOCTEXT("DeleteUnitTitle", "删除单位"));
	if (Answer != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteUnitRow", "Delete Unit Row"));
	Session->Table->Modify();

	// 先收集引用（删行以后就拿不到了）
	TArray<UObject*> AssetsToDelete;
	if (FAsceEntityDefRow* Row = GetSelectedRow())
	{
		if (UAsceUnitModuleConfig* RowConfig = UAsceUnitModuleConfig::LoadForDef(*Row))
		{
			TouchedAssets.Remove(RowConfig);
			AssetsToDelete.Add(RowConfig);
		}
		if (UMassEntityConfigAsset* Template = Row->EntityTemplate.LoadSynchronous())
		{
			TouchedAssets.Remove(Template);
			AssetsToDelete.Add(Template);
		}
	}

	if (Config)
	{
		Config->RemoveFromRoot();
		Config = nullptr;
	}

	Session->Table->RemoveRow(RowName);
	Session->Table->HandleDataTableChanged();
	Session->Table->MarkPackageDirty();

	for (UObject* Asset : AssetsToDelete)
	{
		ObjectTools::DeleteSingleObject(Asset, /*bShowConfirmation*/ false);
	}

	// 先把"删行 + 删资产"落盘，再重建列表（否则 LoadConfigForRow 会给新选中的行自动建配置，混进保存列表）
	const TArray<FString> Saved = SaveTouchedPackages();

	Session->RowName = NAME_None;
	RebuildRowItems();
	LoadConfigForRow();

	FString Message = FString::Printf(TEXT("已删除单位 [%s]（资产 %d 个）。"), *RowName.ToString(), AssetsToDelete.Num());
	if (Saved.Num() > 0)
	{
		Message += FString::Printf(TEXT("\n已保存：%s"), *FString::Join(Saved, TEXT("，")));
	}
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message), LOCTEXT("DeleteUnitDoneTitle", "删除完成"));
	RefreshPreview();
	return FReply::Handled();
}

void SAsceUnitModuleEditor::OpenWindow()
{
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Asce：单位模块化编辑器"))
		.ClientSize(FVector2D(1280.f, 820.f))
		.SupportsMinimize(true)
		.SupportsMaximize(true);

	Window->SetContent(SNew(SAsceUnitModuleEditor));
	FSlateApplication::Get().AddWindow(Window);
}

void RegisterAsceUnitModuleEditorMenus()
{
	FToolMenuOwnerScoped OwnerScoped(TEXT("AsceUnitModuleEditor"));

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = Menu->FindOrAddSection("AsceTools");
	Section.AddMenuEntry(
		"AsceUnitModuleEditor",
		LOCTEXT("MenuEntry", "Asce：单位模块化编辑器"),
		LOCTEXT("MenuEntryTooltip", "按模块配置单位类型（生成 fragment / 写表）"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&SAsceUnitModuleEditor::OpenWindow)));
}

#undef LOCTEXT_NAMESPACE
FReply SAsceUnitModuleEditor::OnStatsClicked()
{
	StatsReport.Reset();

	if (!Session || !Session->Table)
	{
		StatsReport = TEXT("没有选中实体定义表");
		return FReply::Handled();
	}

	// 组合签名 → 该组合包含的行
	struct FGroup
	{
		TArray<FName> Rows;
		int32 FragmentCount = 0;
		int32 BytesPerEntity = 0;
	};
	TMap<FString, FGroup> Groups;
	int32 TotalRows = 0;
	int32 RowsWithConfig = 0;

	const TMap<FName, uint8*>& RowMap = Session->Table->GetRowMap();
	for (const TPair<FName, uint8*>& Pair : RowMap)
	{
		const FAsceEntityDefRow* Row = reinterpret_cast<const FAsceEntityDefRow*>(Pair.Value);
		if (!Row)
		{
			continue;
		}
		++TotalRows;

		const UAsceUnitModuleConfig* RowConfig = UAsceUnitModuleConfig::LoadForDef(*Row);
		if (!RowConfig)
		{
			continue;
		}
		++RowsWithConfig;

		const FAsceUnitModuleBuildResult Result = AsceUnitModuleGenerator::Build(RowConfig->Modules);

		// 签名 = 排序后的 fragment 类型名 + tag 类型名（= archetype 身份）
		TArray<FString> TypeNames;
		TypeNames.Reserve(Result.Fragments.Num() + Result.Tags.Num());
		int32 Bytes = 0;
		for (const FInstancedStruct& Fragment : Result.Fragments)
		{
			if (const UScriptStruct* Type = Fragment.GetScriptStruct())
			{
				TypeNames.Add(FString::Printf(TEXT("F:%s"), *Type->GetName()));
				Bytes += Type->GetStructureSize();
			}
		}
		for (const UScriptStruct* TagType : Result.Tags)
		{
			if (TagType)
			{
				TypeNames.Add(FString::Printf(TEXT("T:%s"), *TagType->GetName()));
			}
		}
		TypeNames.Sort();
		const FString Signature = FString::Join(TypeNames, TEXT("|"));

		FGroup& Group = Groups.FindOrAdd(Signature);
		Group.Rows.Add(Pair.Key);
		Group.FragmentCount = Result.Fragments.Num();
		Group.BytesPerEntity = Bytes;
	}

	// 估算每 chunk 实体数（chunk 大小来自 UMassEntitySettings，按 payload 近似）
	const int32 ChunkBytes = FMath::Max(static_cast<int32>(GetDefault<UMassEntitySettings>()->ChunkMemorySize), 1024);

	TArray<TPair<FString, FGroup>> Sorted;
	for (const TPair<FString, FGroup>& Pair : Groups)
	{
		Sorted.Add(Pair);
	}
	Sorted.Sort([](const TPair<FString, FGroup>& A, const TPair<FString, FGroup>& B)
	{
		return A.Value.Rows.Num() > B.Value.Rows.Num();
	});

	StatsReport = FString::Printf(TEXT("实体定义行 %d（有模块配置 %d），archetype 组合 %d 种（chunk=%dKB）"),
		TotalRows, RowsWithConfig, Sorted.Num(), ChunkBytes / 1024);

	int32 Index = 0;
	for (const TPair<FString, FGroup>& Entry : Sorted)
	{
		++Index;
		const FGroup& Group = Entry.Value;
		const int32 Payload = FMath::Max(Group.BytesPerEntity + 8, 1);   // + 实体句柄
		const int32 PerChunk = FMath::Max(ChunkBytes / Payload, 1);

		FString RowList;
		for (int32 RowIndex = 0; RowIndex < Group.Rows.Num(); ++RowIndex)
		{
			if (RowIndex > 0)
			{
				RowList += TEXT(", ");
			}
			RowList += Group.Rows[RowIndex].ToString();
			if (RowIndex >= 7)
			{
				RowList += FString::Printf(TEXT(" …+%d"), Group.Rows.Num() - RowIndex - 1);
				break;
			}
		}

		StatsReport += FString::Printf(TEXT("\n#%d  %d 行  fragment %d 个  ≈%dB/实体  ≈%d/块\n    %s"),
			Index, Group.Rows.Num(), Group.FragmentCount, Group.BytesPerEntity, PerChunk, *RowList);
	}


	RefreshPreview();
	return FReply::Handled();
}