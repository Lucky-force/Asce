// AsceRowNameWrapperCustomization.cpp
#include "AsceRowNameWrapperCustomization.h"

#include "DataTableSystem/AsceDataTableSubsystem.h"
#include "DataTableSystem/AsceRowNameWrapper.h"

#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Framework/Application/SlateApplication.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "AsceRowNameWrapper"

namespace
{
	/** 下拉里的一行（= 表里的一行 + 预拼好的显示文本）。 */
	struct FAsceRowOption
	{
		FName RowName;
		FString RowNameText;
		FString FieldsText;
		FString SearchText;
		int32 Index = INDEX_NONE;
	};

	using FAsceRowOptionPtr = TSharedPtr<FAsceRowOption>;

	/** 下拉的共享状态（被 widget/委托强引用；widget 销毁后自动释放）。 */
	struct FAsceRowNameState
	{
		FName Alias;
		TArray<FName> FieldNames;
		TSharedPtr<IPropertyHandle> RowNameHandle;
		TWeakPtr<SComboButton> ComboButton;
		TWeakPtr<SListView<FAsceRowOptionPtr>> ListView;

		TArray<FAsceRowOptionPtr> AllOptions;
		TArray<FAsceRowOptionPtr> FilteredOptions;
		FString Filter;

		/** 表解析失败原因（空 = 正常）。 */
		FString Warning;
	};

	/** 字段值 → 显示文本（FText/枚举特判，其余走 ExportTextItem）。 */
	FString GetFieldValueText(const FProperty* Prop, const void* ValuePtr)
	{
		if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
		{
			return TextProp->GetPropertyValue(ValuePtr).ToString();
		}
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			if (const UEnum* Enum = EnumProp->GetEnum())
			{
				const int64 Value = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
				return Enum->GetDisplayNameTextByValue(Value).ToString();
			}
		}
		else if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			if (const UEnum* Enum = ByteProp->Enum)
			{
				return Enum->GetDisplayNameTextByValue(ByteProp->GetPropertyValue(ValuePtr)).ToString();
			}
		}

		FString ValueStr;
		Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
		return ValueStr;
	}

	FString MakeFieldsText(const UScriptStruct* RowStruct, uint8* RowData, const TArray<FName>& FieldNames)
	{
		if (!RowStruct || !RowData)
		{
			return FString();
		}

		FString Out;
		for (const FName FieldName : FieldNames)
		{
			const FProperty* Prop = FindFProperty<FProperty>(RowStruct, FieldName);
			if (!Prop)
			{
				continue;
			}
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(RowData);
			const FString ValueStr = GetFieldValueText(Prop, ValuePtr);
			if (!Out.IsEmpty())
			{
				Out += TEXT("   ");
			}
			Out += FString::Printf(TEXT("%s=%s"), *Prop->GetName(), *ValueStr);
		}
		return Out;
	}

	void ApplyFilter(const TSharedRef<FAsceRowNameState>& State)
	{
		const FString Needle = State->Filter.ToLower();
		State->FilteredOptions.Reset(State->AllOptions.Num());
		for (const FAsceRowOptionPtr& Option : State->AllOptions)
		{
			if (Needle.IsEmpty() || Option->SearchText.Contains(Needle))
			{
				State->FilteredOptions.Add(Option);
			}
		}
		if (TSharedPtr<SListView<FAsceRowOptionPtr>> ListView = State->ListView.Pin())
		{
			ListView->RequestListRefresh();
		}
	}

	/** 每次打开菜单时重建行列表（懒刷新：编辑器里 SDT 挂/卸后能立刻看到）。 */
	void RebuildOptions(const TSharedRef<FAsceRowNameState>& State)
	{
		State->AllOptions.Reset();
		State->Filter.Reset();
		State->Warning.Reset();

		UDataTable* Table = nullptr;
		if (const UAsceDataTableSubsystem* Sub = UAsceDataTableSubsystem::Get())
		{
			Table = Sub->GetMasterTableByAlias(State->Alias);
		}
		if (!Table)
		{
			State->Warning = FString::Printf(
				TEXT("别名 %s 解析不到 MDT（检查 Project Settings → Asce Data Tables / GameFeature 是否激活）"),
				*State->Alias.ToString());
			ApplyFilter(State);
			return;
		}

		const TMap<FName, uint8*>& RowMap = Table->GetRowMap();
		TArray<FName> RowNames;
		RowMap.GenerateKeyArray(RowNames);
		RowNames.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

		// 第一项：清空（None）
		{
			FAsceRowOptionPtr Option = MakeShared<FAsceRowOption>();
			Option->RowName = NAME_None;
			Option->RowNameText = TEXT("None");
			Option->SearchText = TEXT("none");
			State->AllOptions.Add(Option);
		}

		const UScriptStruct* RowStruct = Table->GetRowStruct();
		for (int32 Index = 0; Index < RowNames.Num(); ++Index)
		{
			uint8* const* Found = RowMap.Find(RowNames[Index]);
			FAsceRowOptionPtr Option = MakeShared<FAsceRowOption>();
			Option->RowName = RowNames[Index];
			Option->RowNameText = RowNames[Index].ToString();
			Option->Index = Index;
			Option->FieldsText = MakeFieldsText(RowStruct, Found ? *Found : nullptr, State->FieldNames);
			Option->SearchText = (Option->RowNameText + TEXT(" ") + Option->FieldsText).ToLower();
			State->AllOptions.Add(Option);
		}

		ApplyFilter(State);
	}

	FName GetCurrentRowName(const TSharedRef<FAsceRowNameState>& State)
	{
		FName Current = NAME_None;
		if (State->RowNameHandle.IsValid())
		{
			State->RowNameHandle->GetValue(Current);
		}
		return Current;
	}

	/** 按别名解析 MDT（编辑器里也生效：子系统是引擎子系统）。 */
	UDataTable* ResolveTable(const TSharedRef<FAsceRowNameState>& State)
	{
		if (State->Alias.IsNone())
		{
			return nullptr;
		}
		const UAsceDataTableSubsystem* Sub = UAsceDataTableSubsystem::Get();
		return Sub ? Sub->GetMasterTableByAlias(State->Alias) : nullptr;
	}

	/** RowName 非空但在表里找不到（表不存在也算缺失）。 */
	bool IsRowNameMissing(const TSharedRef<FAsceRowNameState>& State, const FName& Current)
	{
		if (Current.IsNone())
		{
			return false;
		}
		const UDataTable* Table = ResolveTable(State);
		return Table ? !Table->GetRowMap().Contains(Current) : true;
	}

	FText GetBrowseTooltip(const TSharedRef<FAsceRowNameState>& State)
	{
		const UDataTable* Table = ResolveTable(State);
		if (!Table)
		{
			return LOCTEXT("BrowseNoTable", "别名未解析到 MDT（先激活 GameFeature / 检查 Project Settings）");
		}
		return FText::Format(
			LOCTEXT("BrowseToTableFmt", "浏览到 '{0}'（Alt+点击：打开表）"),
			FText::FromString(Table->GetName()));
	}

	FReply OnBrowseClicked(const TSharedRef<FAsceRowNameState>& State)
	{
		UDataTable* Table = ResolveTable(State);
		if (!Table)
		{
			return FReply::Handled();
		}
		if (FSlateApplication::Get().GetModifierKeys().IsAltDown())
		{
			GEditor->EditObject(Table);   // 打开 DataTable 编辑器
		}
		else
		{
			TArray<UObject*> Objects;
			Objects.Add(Table);
			GEditor->SyncBrowserToObjects(Objects);   // 在内容浏览器里定位
		}
		return FReply::Handled();
	}

	TSharedRef<SWidget> BuildMenu(const TSharedRef<FAsceRowNameState>& State)
	{
		RebuildOptions(State);

		TSharedPtr<SListView<FAsceRowOptionPtr>> ListView;
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

		if (!State->Warning.IsEmpty())
		{
			Box->AddSlot()
				.AutoHeight()
				.Padding(6.f, 4.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(State->Warning))
					.ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.35f, 0.35f)))
					.AutoWrapText(true)
				];
		}

		Box->AddSlot()
			.AutoHeight()
			.Padding(4.f, 2.f)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("FilterHint", "过滤..."))
				.OnTextChanged_Lambda([State](const FText& Text)
				{
					State->Filter = Text.ToString();
					ApplyFilter(State);
				})
			];

		Box->AddSlot()
			.FillHeight(1.f)
			[
				SAssignNew(ListView, SListView<FAsceRowOptionPtr>)
				.ListItemsSource(&State->FilteredOptions)
				.OnGenerateRow_Lambda([](FAsceRowOptionPtr Option, const TSharedRef<STableViewBase>& OwnerTable)
				{
					return SNew(STableRow<FAsceRowOptionPtr>, OwnerTable)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4.f, 1.f)
						[
							SNew(STextBlock)
							.Text_Lambda([Option]()
							{
								return FText::FromString(Option->Index >= 0 ? FString::FromInt(Option->Index) : TEXT("-"));
							})
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4.f, 1.f)
						[
							SNew(STextBlock)
							.Text_Lambda([Option]() { return FText::FromString(Option->RowNameText); })
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Center)
						.Padding(8.f, 1.f)
						[
							SNew(STextBlock)
							.Text_Lambda([Option]() { return FText::FromString(Option->FieldsText); })
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					];
				})
				.OnSelectionChanged_Lambda([State](FAsceRowOptionPtr Option, ESelectInfo::Type SelectInfo)
				{
					if (!Option.IsValid() || SelectInfo == ESelectInfo::Direct)
					{
						return;
					}
					if (State->RowNameHandle.IsValid())
					{
						State->RowNameHandle->SetValue(Option->RowName);
					}
					if (TSharedPtr<SComboButton> ComboButton = State->ComboButton.Pin())
					{
						ComboButton->SetIsOpen(false);
					}
				})
			];

		State->ListView = ListView;

		return SNew(SBox)
			.WidthOverride(520.f)
			.MaxDesiredHeight(420.f)
			[
				Box
			];
	}
} // namespace

TSharedRef<IPropertyTypeCustomization> FAsceRowNameWrapperCustomization::MakeInstance()
{
	return MakeShareable(new FAsceRowNameWrapperCustomization());
}

void FAsceRowNameWrapperCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const FProperty* Property = PropertyHandle->GetProperty();
	const FString Binding = Property ? Property->GetMetaData(TEXT("DataTableBinding")) : FString();

	// 解析 "<别名>[:<字段1>,<字段2>...]"
	FString AliasPart = Binding;
	FString FieldsPart;
	if (!Binding.Split(TEXT(":"), &AliasPart, &FieldsPart))
	{
		FieldsPart.Reset();
	}
	AliasPart.TrimStartAndEndInline();

	TSharedRef<FAsceRowNameState> State = MakeShared<FAsceRowNameState>();
	State->Alias = FName(*AliasPart);
	State->RowNameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAsceRowNameWrapper, RowName));

	if (!FieldsPart.IsEmpty())
	{
		TArray<FString> FieldStrings;
		FieldsPart.ParseIntoArray(FieldStrings, TEXT(","), /*CullEmpty=*/true);
		for (FString& FieldString : FieldStrings)
		{
			FieldString.TrimStartAndEndInline();
			if (!FieldString.IsEmpty())
			{
				State->FieldNames.Add(FName(*FieldString));
			}
		}
	}

	// 没绑定别名 / 拿不到子属性：回退成默认编辑（裸行名）
	if (State->Alias.IsNone() || !State->RowNameHandle.IsValid())
	{
		HeaderRow
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				PropertyHandle->CreatePropertyValueWidget()
			];
		return;
	}

	TSharedPtr<SComboButton> ComboButtonWidget;
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(240.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(ComboButtonWidget, SComboButton)
				.HasDownArrow(true)
				.ContentPadding(FMargin(4.f, 1.f))
				.OnGetMenuContent_Lambda([State]() { return BuildMenu(State); })
				.OnMenuOpenChanged_Lambda([State](bool bOpen)
				{
					if (!bOpen)
					{
						State->Filter.Reset();
					}
				})
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([State]()
					{
						const FName Current = GetCurrentRowName(State);
						return Current.IsNone() ? LOCTEXT("NoneRow", "None") : FText::FromName(Current);
					})
					.ColorAndOpacity_Lambda([State]()
					{
						const FName Current = GetCurrentRowName(State);
						if (Current.IsNone())
						{
							return FSlateColor::UseSubduedForeground();
						}
						return IsRowNameMissing(State, Current)
							? FSlateColor(FLinearColor(1.f, 0.35f, 0.35f))
							: FSlateColor::UseForeground();
					})
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(1.f, 0.f))
				.Visibility_Lambda([State]()
				{
					return ResolveTable(State) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.ToolTipText_Lambda([State]() { return GetBrowseTooltip(State); })
				.OnClicked_Lambda([State]() { return OnBrowseClicked(State); })
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.BrowseContent"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		];

	State->ComboButton = ComboButtonWidget;
}

#undef LOCTEXT_NAMESPACE
