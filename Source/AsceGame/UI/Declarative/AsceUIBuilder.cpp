// AsceUIBuilder.cpp
#include "AsceUIBuilder.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "CommonButtonBase.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"

#include "AsceUIThemeSettings.h"

namespace
{
	/** 槽位对齐：UE5.7 的槽位类型各自实现，逐个处理 */
	void SetSlotAlign(UPanelSlot* Slot, const EHorizontalAlignment Horizontal, const EVerticalAlignment Vertical)
	{
		if (!Slot)
		{
			return;
		}
		if (UVerticalBoxSlot* VBoxSlot = Cast<UVerticalBoxSlot>(Slot))
		{
			VBoxSlot->SetHorizontalAlignment(Horizontal);
			VBoxSlot->SetVerticalAlignment(Vertical);
		}
		else if (UHorizontalBoxSlot* HBoxSlot = Cast<UHorizontalBoxSlot>(Slot))
		{
			HBoxSlot->SetHorizontalAlignment(Horizontal);
			HBoxSlot->SetVerticalAlignment(Vertical);
		}
		else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Slot))
		{
			OverlaySlot->SetHorizontalAlignment(Horizontal);
			OverlaySlot->SetVerticalAlignment(Vertical);
		}
		else if (UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(Slot))
		{
			ScrollSlot->SetHorizontalAlignment(Horizontal);
			ScrollSlot->SetVerticalAlignment(Vertical);
		}
		else if (UBorderSlot* BorderSlot = Cast<UBorderSlot>(Slot))
		{
			BorderSlot->SetHorizontalAlignment(Horizontal);
			BorderSlot->SetVerticalAlignment(Vertical);
		}
		else if (USizeBoxSlot* SizeSlot = Cast<USizeBoxSlot>(Slot))
		{
			SizeSlot->SetHorizontalAlignment(Horizontal);
			SizeSlot->SetVerticalAlignment(Vertical);
		}
		else if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Slot))
		{
			ButtonSlot->SetHorizontalAlignment(Horizontal);
			ButtonSlot->SetVerticalAlignment(Vertical);
		}
	}

	/** 槽位内边距：UE5.7 没有 UBoxSlot 基类，逐个类型处理 */
	void SetSlotPadding(UPanelSlot* Slot, const FMargin& Padding)
	{
		if (!Slot)
		{
			return;
		}
		if (UVerticalBoxSlot* BoxSlot = Cast<UVerticalBoxSlot>(Slot))
		{
			BoxSlot->SetPadding(Padding);
		}
		else if (UHorizontalBoxSlot* HBoxSlot = Cast<UHorizontalBoxSlot>(Slot))
		{
			HBoxSlot->SetPadding(Padding);
		}
		else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Slot))
		{
			OverlaySlot->SetPadding(Padding);
		}
		else if (UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(Slot))
		{
			ScrollSlot->SetPadding(Padding);
		}
		else if (UBorderSlot* BorderSlot = Cast<UBorderSlot>(Slot))
		{
			BorderSlot->SetPadding(Padding);
		}
		else if (USizeBoxSlot* SizeSlot = Cast<USizeBoxSlot>(Slot))
		{
			SizeSlot->SetPadding(Padding);
		}
		else if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Slot))
		{
			ButtonSlot->SetPadding(Padding);
		}
	}
}

void UAsceUIBuilder::Initialize(UWidgetTree* InWidgetTree, UAsceUIStyleSet* InStyleSet, UUserWidget* InOwnerWidget)
{
	WidgetTree = InWidgetTree;
	StyleSet = InStyleSet;
	OwnerWidget = InOwnerWidget;

	Scopes.Reset();
	Errors.Reset();
	IdToWidget.Reset();
	RootWidget = nullptr;
	LastWidget = nullptr;
}

void UAsceUIBuilder::AddError(const FString& Message)
{
	Errors.Add(FText::FromString(Message));
}

UClass* UAsceUIBuilder::ResolveChecked(UClass* ConfiguredClass, UClass* RequiredBaseClass, UClass* FallbackClass, const TCHAR* What)
{
	if (!ConfiguredClass)
	{
		return FallbackClass;
	}
	if (RequiredBaseClass && !ConfiguredClass->IsChildOf(RequiredBaseClass))
	{
		AddError(FString::Printf(TEXT("[UI] 样式 %s 的控件类 %s 不是 %s 派生，已回退到默认类"),
			What, *ConfiguredClass->GetName(), *RequiredBaseClass->GetName()));
		return FallbackClass;
	}
	return ConfiguredClass;
}

UWidget* UAsceUIBuilder::CreateWidgetOfClass(UClass* WidgetClass, const FName Name)
{
	if (!WidgetTree || !WidgetClass)
	{
		return nullptr;
	}

	const FName WidgetName = Name.IsNone() ? NAME_None : Name;
	if (WidgetClass->IsChildOf(UUserWidget::StaticClass()))
	{
		return CreateWidget<UUserWidget>(OwnerWidget, WidgetClass, WidgetName);
	}
	return WidgetTree->ConstructWidget<UWidget>(WidgetClass, WidgetName);
}

void UAsceUIBuilder::PushScope(UWidget* Container, const bool bContentWidget, const float Spacing)
{
	FScope Scope;
	Scope.Container = Container;
	Scope.bContentWidget = bContentWidget;
	Scope.Spacing = Spacing;
	Scopes.Add(Scope);
}

bool UAsceUIBuilder::AttachToCurrent(UWidget* Widget)
{
	if (!Widget)
	{
		return false;
	}

	// 没有作用域 ⇒ 这是根节点（只能有一个）
	if (Scopes.Num() == 0)
	{
		if (RootWidget && RootWidget != Widget)
		{
			AddError(FString::Printf(TEXT("[UI] 出现第二个根节点（%s），已忽略（一份布局只能有一个根）"),
				*Widget->GetName()));
			return false;
		}
		RootWidget = Widget;
		return true;
	}

	FScope& Scope = Scopes.Last();

	if (Scope.bContentWidget)
	{
		UContentWidget* Content = Cast<UContentWidget>(Scope.Container);
		if (!Content)
		{
			AddError(FString::Printf(TEXT("[UI] 容器 %s 不是 UContentWidget"), *GetNameSafe(Scope.Container)));
			return false;
		}
		if (Scope.ChildCount > 0)
		{
			AddError(FString::Printf(TEXT("[UI] 容器 %s 只能有 1 个子节点，多余的（%s）已忽略"),
				*GetNameSafe(Scope.Container), *Widget->GetName()));
			return false;
		}
		UPanelSlot* Slot = Content->SetContent(Widget);
		SetSlotPadding(Slot, FMargin(0.f));
		++Scope.ChildCount;
		return true;
	}

	UPanelWidget* Panel = Cast<UPanelWidget>(Scope.Container);
	if (!Panel)
	{
		AddError(FString::Printf(TEXT("[UI] 容器 %s 不是 UPanelWidget"), *GetNameSafe(Scope.Container)));
		return false;
	}
	UPanelSlot* Slot = Panel->AddChild(Widget);
	if (!Slot)
	{
		AddError(FString::Printf(TEXT("[UI] 把 %s 挂到 %s 失败"), *Widget->GetName(), *Panel->GetName()));
		return false;
	}

	// 子节点间隔：竖排加在顶部、横排加在左侧（第一个不加）
	if (Scope.Spacing > 0.f && Scope.ChildCount > 0)
	{
		FMargin SpacingMargin;
		if (Panel->IsA(UVerticalBox::StaticClass()))
		{
			SpacingMargin.Top = Scope.Spacing;
		}
		else if (Panel->IsA(UHorizontalBox::StaticClass()))
		{
			SpacingMargin.Left = Scope.Spacing;
		}
		else
		{
			SpacingMargin = FMargin(Scope.Spacing);
		}
		SetSlotPadding(Slot, SpacingMargin);
	}
	++Scope.ChildCount;
	return true;
}

void UAsceUIBuilder::ApplySlotPadding(UPanelSlot* Slot, const EAsceUISpace Padding, const float ExtraSpacing)
{
	const float Value = StyleSet ? StyleSet->GetSpace(Padding) : 0.f;
	SetSlotPadding(Slot, FMargin(Value + ExtraSpacing));
}

UAsceUIBuilder* UAsceUIBuilder::BeginContainer(UClass* WidgetClass, const FName Id, const EAsceUISpace Spacing, const bool bSingleChild)
{
	if (!WidgetClass)
	{
		AddError(FString::Printf(TEXT("[UI] 容器 %s 没有可用控件类"), *Id.ToString()));
		return this;
	}

	UWidget* Container = CreateWidgetOfClass(WidgetClass, Id);
	if (!Container)
	{
		AddError(FString::Printf(TEXT("[UI] 创建容器 %s (%s) 失败"), *Id.ToString(), *WidgetClass->GetName()));
		return this;
	}
	if (!Id.IsNone() && IdToWidget.Contains(Id))
	{
		AddError(FString::Printf(TEXT("[UI] id 重复：%s"), *Id.ToString()));
	}
	else if (!Id.IsNone())
	{
		IdToWidget.Add(Id, Container);
	}

	AttachToCurrent(Container);
	LastWidget = Container;

	const float SpacingValue = StyleSet ? StyleSet->GetSpace(Spacing) : 0.f;
	PushScope(Container, bSingleChild, SpacingValue);
	return this;
}

UAsceUIBuilder* UAsceUIBuilder::VerticalBox(const FName Id, const EAsceUISpace Spacing)
{
	return BeginContainer(UVerticalBox::StaticClass(), Id, Spacing, false);
}

UAsceUIBuilder* UAsceUIBuilder::HorizontalBox(const FName Id, const EAsceUISpace Spacing)
{
	return BeginContainer(UHorizontalBox::StaticClass(), Id, Spacing, false);
}

UAsceUIBuilder* UAsceUIBuilder::Overlay(const FName Id)
{
	return BeginContainer(UOverlay::StaticClass(), Id, EAsceUISpace::None, false);
}

UAsceUIBuilder* UAsceUIBuilder::ScrollBox(const FName Id)
{
	return BeginContainer(UScrollBox::StaticClass(), Id, EAsceUISpace::None, false);
}

UAsceUIBuilder* UAsceUIBuilder::CanvasPanel(const FName Id)
{
	return BeginContainer(UCanvasPanel::StaticClass(), Id, EAsceUISpace::None, false);
}

UAsceUIBuilder* UAsceUIBuilder::SizeBox(const FName Id)
{
	return BeginContainer(USizeBox::StaticClass(), Id, EAsceUISpace::None, true);
}

UAsceUIBuilder* UAsceUIBuilder::Border(const FName Id, const EAsceUIPanelStyleToken Style)
{
	UClass* Class = StyleSet ? StyleSet->FindPanelClass(Style) : nullptr;
	Class = ResolveChecked(Class, UBorder::StaticClass(), UBorder::StaticClass(), TEXT("Panel"));
	return BeginContainer(Class, Id, EAsceUISpace::None, true);
}

UAsceUIBuilder* UAsceUIBuilder::Text(const FName Id, const FText& InText, const EAsceUITextStyleToken Style)
{
	UClass* Class = StyleSet ? StyleSet->FindTextClass(Style) : nullptr;
	Class = ResolveChecked(Class, UTextBlock::StaticClass(), UTextBlock::StaticClass(), TEXT("Text"));

	UTextBlock* Widget = Cast<UTextBlock>(CreateWidgetOfClass(Class, Id));
	if (!Widget)
	{
		AddError(FString::Printf(TEXT("[UI] 创建文本 %s 失败"), *Id.ToString()));
		return this;
	}
	Widget->SetText(InText);

	if (!Id.IsNone() && IdToWidget.Contains(Id))
	{
		AddError(FString::Printf(TEXT("[UI] id 重复：%s"), *Id.ToString()));
	}
	else if (!Id.IsNone())
	{
		IdToWidget.Add(Id, Widget);
	}

	AttachToCurrent(Widget);
	LastWidget = Widget;
	return this;
}

UAsceUIBuilder* UAsceUIBuilder::Image(const FName Id, TSoftObjectPtr<UTexture2D> InTexture, const EAsceUIImageStyleToken Style)
{
	UClass* Class = StyleSet ? StyleSet->FindImageClass(Style) : nullptr;
	Class = ResolveChecked(Class, UImage::StaticClass(), UImage::StaticClass(), TEXT("Image"));

	UImage* Widget = Cast<UImage>(CreateWidgetOfClass(Class, Id));
	if (!Widget)
	{
		AddError(FString::Printf(TEXT("[UI] 创建图片 %s 失败"), *Id.ToString()));
		return this;
	}
	if (UTexture2D* Texture = InTexture.LoadSynchronous())
	{
		Widget->SetBrushFromTexture(Texture);
	}

	if (!Id.IsNone() && IdToWidget.Contains(Id))
	{
		AddError(FString::Printf(TEXT("[UI] id 重复：%s"), *Id.ToString()));
	}
	else if (!Id.IsNone())
	{
		IdToWidget.Add(Id, Widget);
	}

	AttachToCurrent(Widget);
	LastWidget = Widget;
	return this;
}

UAsceUIBuilder* UAsceUIBuilder::Button(const FName Id, const FText& InLabel, const EAsceUIButtonStyleToken Style)
{
	UClass* Class = StyleSet ? StyleSet->FindButtonClass(Style) : nullptr;
	if (Class && !Class->IsChildOf(UButton::StaticClass()) && !Class->IsChildOf(UCommonButtonBase::StaticClass()))
	{
		AddError(FString::Printf(TEXT("[UI] 按钮样式类的 %s 既不是 UButton 也不是 UCommonButtonBase 派生，已回退"), *Class->GetName()));
		Class = UButton::StaticClass();
	}
	if (!Class)
	{
		Class = UButton::StaticClass();
	}

	UWidget* Widget = CreateWidgetOfClass(Class, Id);
	if (!Widget)
	{
		AddError(FString::Printf(TEXT("[UI] 创建按钮 %s 失败"), *Id.ToString()));
		return this;
	}

	// 文字：UButton 直接加一个 TextBlock 子节点；CommonButton 派生类要求提供 SetLabel(FText)
	if (UButton* SimpleButton = Cast<UButton>(Widget))
	{
		if (UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Label")))
		{
			Label->SetText(InLabel);
			Label->SetJustification(ETextJustify::Center);
			SimpleButton->AddChild(Label);
		}
	}
	else if (UCommonButtonBase* CommonButton = Cast<UCommonButtonBase>(Widget))
	{
		if (UFunction* SetLabelFn = CommonButton->FindFunction(TEXT("SetLabel")))
		{
			struct FLabelParams { FText Label; };
			FLabelParams Params;
			Params.Label = InLabel;
			CommonButton->ProcessEvent(SetLabelFn, &Params);
		}
		else
		{
			AddError(FString::Printf(TEXT("[UI] 按钮类 %s 需要提供 UFUNCTION SetLabel(FText)（WBP 子类请实现它）"),
				*Class->GetName()));
		}
	}

	if (!Id.IsNone() && IdToWidget.Contains(Id))
	{
		AddError(FString::Printf(TEXT("[UI] id 重复：%s"), *Id.ToString()));
	}
	else if (!Id.IsNone())
	{
		IdToWidget.Add(Id, Widget);
	}

	AttachToCurrent(Widget);
	LastWidget = Widget;
	return this;
}

UAsceUIBuilder* UAsceUIBuilder::ProgressBar(const FName Id, const float InPercent, const EAsceUIBarStyleToken Style)
{
	UClass* Class = StyleSet ? StyleSet->FindBarClass(Style) : nullptr;
	Class = ResolveChecked(Class, UProgressBar::StaticClass(), UProgressBar::StaticClass(), TEXT("Bar"));

	UProgressBar* Widget = Cast<UProgressBar>(CreateWidgetOfClass(Class, Id));
	if (!Widget)
	{
		AddError(FString::Printf(TEXT("[UI] 创建进度条 %s 失败"), *Id.ToString()));
		return this;
	}
	Widget->SetPercent(FMath::Clamp(InPercent, 0.f, 1.f));

	if (!Id.IsNone() && IdToWidget.Contains(Id))
	{
		AddError(FString::Printf(TEXT("[UI] id 重复：%s"), *Id.ToString()));
	}
	else if (!Id.IsNone())
	{
		IdToWidget.Add(Id, Widget);
	}

	AttachToCurrent(Widget);
	LastWidget = Widget;
	return this;
}

UAsceUIBuilder* UAsceUIBuilder::Spacer(const FName Id)
{
	USpacer* Widget = Cast<USpacer>(CreateWidgetOfClass(USpacer::StaticClass(), Id));
	if (!Widget)
	{
		AddError(FString::Printf(TEXT("[UI] 创建占位 %s 失败"), *Id.ToString()));
		return this;
	}

	if (!Id.IsNone() && IdToWidget.Contains(Id))
	{
		AddError(FString::Printf(TEXT("[UI] id 重复：%s"), *Id.ToString()));
	}
	else if (!Id.IsNone())
	{
		IdToWidget.Add(Id, Widget);
	}

	AttachToCurrent(Widget);
	LastWidget = Widget;
	return this;
}

UAsceUIBuilder* UAsceUIBuilder::Padding(const EAsceUISpace InPadding)
{
	if (!LastWidget)
	{
		AddError(TEXT("[UI] Padding：当前没有控件"));
		return this;
	}
	if (!LastWidget->Slot)
	{
		AddError(FString::Printf(TEXT("[UI] Padding：%s 是根节点（没有父槽位），内边距被忽略"), *LastWidget->GetName()));
		return this;
	}
	ApplySlotPadding(LastWidget->Slot, InPadding, 0.f);
	return this;
}

UAsceUIBuilder* UAsceUIBuilder::MinSize(const FVector2D InMinSize)
{
	if (USizeBox* SizeBox = Cast<USizeBox>(LastWidget))
	{
		SizeBox->SetMinDesiredWidth(InMinSize.X);
		SizeBox->SetMinDesiredHeight(InMinSize.Y);
	}
	else
	{
		AddError(TEXT("[UI] MinSize：最近创建的控件不是 SizeBox"));
	}
	return this;
}

UAsceUIBuilder* UAsceUIBuilder::MaxSize(const FVector2D InMaxSize)
{
	if (USizeBox* SizeBox = Cast<USizeBox>(LastWidget))
	{
		SizeBox->SetMaxDesiredWidth(InMaxSize.X);
		SizeBox->SetMaxDesiredHeight(InMaxSize.Y);
	}
	else
	{
		AddError(TEXT("[UI] MaxSize：最近创建的控件不是 SizeBox"));
	}
	return this;
}

UAsceUIBuilder* UAsceUIBuilder::SetPercent(const float InPercent)
{
	if (UProgressBar* Bar = Cast<UProgressBar>(LastWidget))
	{
		Bar->SetPercent(FMath::Clamp(InPercent, 0.f, 1.f));
	}
	else
	{
		AddError(TEXT("[UI] SetPercent：最近创建的控件不是 ProgressBar"));
	}
	return this;
}

UAsceUIBuilder* UAsceUIBuilder::Anchors(const FAnchors InAnchors)
{
	if (UCanvasPanelSlot* CanvasSlot = LastWidget ? Cast<UCanvasPanelSlot>(LastWidget->Slot) : nullptr)
	{
		CanvasSlot->SetAnchors(InAnchors);
	}
	else
	{
		AddError(TEXT("[UI] Anchors：最近创建的控件没有挂在 CanvasPanel 上"));
	}
	return this;
}

UAsceUIBuilder* UAsceUIBuilder::Offsets(const FMargin InOffsets)
{
	if (UCanvasPanelSlot* CanvasSlot = LastWidget ? Cast<UCanvasPanelSlot>(LastWidget->Slot) : nullptr)
	{
		CanvasSlot->SetOffsets(InOffsets);
	}
	else
	{
		AddError(TEXT("[UI] Offsets：最近创建的控件没有挂在 CanvasPanel 上"));
	}
	return this;
}

UAsceUIBuilder* UAsceUIBuilder::Align(const EHorizontalAlignment Horizontal, const EVerticalAlignment Vertical)
{
	if (!LastWidget || !LastWidget->Slot)
	{
		AddError(TEXT("[UI] Align：当前控件没有父槽位"));
		return this;
	}
	SetSlotAlign(LastWidget->Slot, Horizontal, Vertical);
	return this;
}

UAsceUIBuilder* UAsceUIBuilder::Select(const FName Id)
{
	if (UWidget* Found = FindWidget(Id))
	{
		LastWidget = Found;
	}
	else
	{
		AddError(FString::Printf(TEXT("[UI] Select：找不到 id %s"), *Id.ToString()));
	}
	return this;
}

UAsceUIBuilder* UAsceUIBuilder::End()
{
	if (Scopes.Num() == 0)
	{
		AddError(TEXT("[UI] End() 多余：没有打开的容器"));
		return this;
	}
	UWidget* Closed = Scopes.Pop().Container;
	LastWidget = Closed;
	return this;
}

UWidget* UAsceUIBuilder::FindWidget(const FName Id) const
{
	const TObjectPtr<UWidget>* Found = IdToWidget.Find(Id);
	return Found ? Found->Get() : nullptr;
}

void UAsceUIBuilder::LogErrors(const FString& Context) const
{
	for (const FText& Error : Errors)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UI] %s %s"), *Context, *Error.ToString());
	}
}
