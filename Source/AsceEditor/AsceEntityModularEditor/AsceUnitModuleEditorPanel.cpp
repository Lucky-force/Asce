// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/AsceUnitModuleEditorPanel.h"

#include "Engine/DataTable.h"
#include "FileHelpers.h"
#include "MassEntitySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "Misc/PackageName.h"
#include "Templates/GuardValueAccessors.h"
#include "PropertyEditorModule.h"
#include "Styling/CoreStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include "AsceMass/Combat/AsceAnimDef.h"
#include "AsceMass/Data/AsceEntityDefTypes.h"

#include "AsceEntityModularEditor/AsceUnitModuleConfig.h"
#include "AsceEntityModularEditor/AsceUnitModuleGenerator.h"
#include "AsceEntityModularEditor/AsceUnitModulePreviewViewport.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleAnimation.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleCombat.h"
#include "AsceEntityModularEditor/Modules/AsceUnitModuleMovement.h"

#define LOCTEXT_NAMESPACE "AsceUnitModuleEditor"

void SAsceUnitModuleEditor::Construct(const FArguments& InArgs)
{
	Session = NewObject<UAsceUnitModuleEditorSession>(GetTransientPackage());
	Session->AddToRoot();

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs SessionArgs;
	SessionArgs.bShowOptions = false;
	SessionArgs.bAllowSearch = false;
	SessionArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	SessionDetails = PropertyEditor.CreateDetailView(SessionArgs);
	SessionDetails->SetObject(Session.Get());
	SessionDetails->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&)
	{
		OnSessionChanged();
	});

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
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SessionDetails.ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
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
							return FText::FromName(Session ? Session->RowName : NAME_None);
						})
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 6.f, 0.f, 0.f)
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
			.AutoHeight()
			.Padding(0.f, 6.f)
			[
				SNew(SBox)
				.HeightOverride(320.f)
				[
					SAssignNew(PreviewViewport, SAsceUnitModulePreviewViewport)
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					ConfigDetails.ToSharedRef()
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 4.f)
			[
				SAssignNew(StatsText, STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)))
				.Text(this, &SAsceUnitModuleEditor::GetStatsText)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(StatusText, STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.45f, 0.35f)))
				.Text(this, &SAsceUnitModuleEditor::GetStatusText)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 2.f)
			[
				SAssignNew(ReportText, STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 1.f)))
				.Text_Lambda([this]() { return FText::FromString(StatsReport); })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 6.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Apply", "应用到 def / 模板"))
					.OnClicked(this, &SAsceUnitModuleEditor::OnApplyClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Save", "保存配置资产"))
					.OnClicked(this, &SAsceUnitModuleEditor::OnSaveClicked)
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
	];

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

	if (FAsceEntityDefRow* Row = GetSelectedRow())
	{
		Config = UAsceUnitModuleConfig::LoadForDef(*Row);
	}
	if (!Config && Session)
	{
		Config = Session->ModuleConfig;
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

	const FString Dirty = Config->IsDirty() ? TEXT("【模块已改，未应用】") : TEXT("已应用");
	return FText::FromString(FString::Printf(TEXT("%s   fragment %d 个 ≈ %dB/实体\n%s"),
		*Dirty, Result.Fragments.Num(), TotalBytes, *Fragments));
}

FText SAsceUnitModuleEditor::GetStatusText() const
{
	return FText::FromString(LastErrors);
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
#if WITH_EDITORONLY_DATA
		// 行里没配时，把当前配置写回行（editor-only 字段）
		if (Row->ModuleConfig.IsNull())
		{
			Row->ModuleConfig = FSoftObjectPath(Config.Get());
		}
#endif
		TArray<FText> Errors;
		if (!Config->ApplyToDef(*Row, Errors))
		{
			for (const FText& Error : Errors)
			{
				LastErrors += Error.ToString() + TEXT("\n");
			}
		}
		if (Session && Session->Table)
		{
			Session->Table->MarkPackageDirty();   // def 行被写了（动画/攻击行/技能槽）
		}
	}

	RefreshPreview();
	return FReply::Handled();
}

FReply SAsceUnitModuleEditor::OnSaveClicked()
{
	if (Config)
	{
		UPackage* Package = Config->GetOutermost();
		Package->MarkPackageDirty();
		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Add(Package);
		FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
	}
	return FReply::Handled();
}

void SAsceUnitModuleEditor::OpenWindow()
{
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Asce：单位模块化编辑器"))
		.ClientSize(FVector2D(760.f, 900.f))
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
