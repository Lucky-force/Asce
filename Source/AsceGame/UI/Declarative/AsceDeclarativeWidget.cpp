// AsceDeclarativeWidget.cpp
#include "AsceDeclarativeWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"

#include "AsceUIBuilder.h"
#include "AsceUIThemeSettings.h"

UAsceDeclarativeWidget::UAsceDeclarativeWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAsceDeclarativeWidget::BuildUI_Implementation(UAsceUIBuilder* InBuilder)
{
	// 默认什么都不建：AS 子类没实现时这里会留下空布局，由 BuildLayoutOnce 记一条错误
}

TSharedRef<SWidget> UAsceDeclarativeWidget::RebuildWidget()
{
	BuildLayoutOnce();
	return Super::RebuildWidget();
}

void UAsceDeclarativeWidget::BuildLayoutOnce()
{
	if (bLayoutBuilt)
	{
		return;
	}
	bLayoutBuilt = true;

	// WidgetTree 正常由 UUserWidget::Initialize 创建；C++-only 类这里兜底
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}

	LayoutBuilder = NewObject<UAsceUIBuilder>(this, TEXT("UIBuilder"));
	LayoutBuilder->Initialize(WidgetTree, GetMutableDefault<UAsceUIStyleSet>(), this);

	BuildUI(LayoutBuilder);

	// 未关闭的作用域 = 布局里 End() 写少了
	if (LayoutBuilder->GetOpenScopeCount() > 0)
	{
		BuildErrors.Add(FText::FromString(FString::Printf(
			TEXT("[UI] %s 的 BuildUI 有 %d 个容器没有 End()"), *GetName(), LayoutBuilder->GetOpenScopeCount())));
	}

	UWidget* Root = LayoutBuilder->GetRootWidget();
	if (!Root)
	{
		BuildErrors.Add(FText::FromString(FString::Printf(
			TEXT("[UI] %s 的 BuildUI 没有创建任何根控件"), *GetName())));
	}
	WidgetTree->RootWidget = Root;

	// builder 的错误也并进来（方便一次性看）
	BuildErrors.Append(LayoutBuilder->GetErrors());
	LayoutBuilder->LogErrors(GetName());
}

UWidget* UAsceDeclarativeWidget::FindWidget(const FName Id) const
{
	return LayoutBuilder ? LayoutBuilder->FindWidget(Id) : nullptr;
}

UPanelWidget* UAsceDeclarativeWidget::FindPanel(const FName Id) const
{
	return Cast<UPanelWidget>(FindWidget(Id));
}
