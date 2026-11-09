// AsceUIBuilder.h
#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "UObject/Object.h"
#include "Widgets/Layout/Anchors.h"

#include "AsceUIDeclarativeTypes.h"

#include "AsceUIBuilder.generated.h"

class UAsceUIStyleSet;
class UPanelSlot;
class UTexture2D;
class UUserWidget;
class UWidget;
class UWidgetTree;

/**
 * **即时 UI builder**：调用即创建控件并挂到当前父节点。
 *
 * 用法（AS）：在 `UAsceDeclarativeWidget::BuildUI` 里按顺序调用：
 * ```angelscript
 * B.VerticalBox(n"Root", EAsceUISpace::M);
 *   B.Text(n"Title", "单位信息", EAsceUITextStyleToken::Title);
 *   B.ProgressBar(n"HpBar", 0.8, EAsceUIBarStyleToken::Health);
 * B.End();
 * ```
 *
 * 约定：
 * - **容器**进入子作用域，必须配 `End()`；`Border/SizeBox` 最多 1 个子节点；
 * - **叶子**（Text/Image/Button/ProgressBar/Spacer）不能挂子节点；
 * - 样式只给 token，控件类从 `UAsceUIStyleSet` 取；**builder 不调样式数据**，只设内容/布局参数；
 * - `Padding/MinSize/Percent/Anchors/...` 作用于**最近创建的控件**；
 * - 错误只记录不崩：未知样式/类型不符/重复 id/作用域不配对 → `Errors`，构建继续（退化到默认类）。
 */
UCLASS(BlueprintType, Transient)
class ASCEGAME_API UAsceUIBuilder : public UObject
{
	GENERATED_BODY()

public:
	/** C++ 入口（`UAsceDeclarativeWidget::RebuildWidget` 调用） */
	void Initialize(UWidgetTree* InWidgetTree, UAsceUIStyleSet* InStyleSet, UUserWidget* InOwnerWidget);

	// ── 容器（进入子作用域，必须 End） ──

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* VerticalBox(FName Id, EAsceUISpace Spacing = EAsceUISpace::None);

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* HorizontalBox(FName Id, EAsceUISpace Spacing = EAsceUISpace::None);

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* Overlay(FName Id);

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* ScrollBox(FName Id);

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* CanvasPanel(FName Id);

	/** Border：单子节点容器；外观来自样式表里的控件类（builder 不设画刷） */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* Border(FName Id, EAsceUIPanelStyleToken Style = EAsceUIPanelStyleToken::Panel);

	/** SizeBox：单子节点容器 */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* SizeBox(FName Id);

	// ── 叶子 ──

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* Text(FName Id, const FText& InText, EAsceUITextStyleToken Style = EAsceUITextStyleToken::Body);

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* Image(FName Id, TSoftObjectPtr<UTexture2D> InTexture, EAsceUIImageStyleToken Style = EAsceUIImageStyleToken::Icon);

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* Button(FName Id, const FText& InLabel, EAsceUIButtonStyleToken Style = EAsceUIButtonStyleToken::Primary);

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* ProgressBar(FName Id, float InPercent = 1.f, EAsceUIBarStyleToken Style = EAsceUIBarStyleToken::Health);

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* Spacer(FName Id);

	// ── 布局参数（作用于最近创建的控件） ──

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* Padding(EAsceUISpace InPadding);

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* MinSize(FVector2D InMinSize);

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* MaxSize(FVector2D InMaxSize);

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* SetPercent(float InPercent);

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* Anchors(FAnchors InAnchors);

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* Offsets(FMargin InOffsets);

	/** 槽位对齐（Box/Overlay/Border/SizeBox/Button 等槽位支持；CanvasPanel 用 Anchors/Offsets） */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* Align(EHorizontalAlignment Horizontal, EVerticalAlignment Vertical);

	/** 把"当前操作目标"切到指定 id 的控件（**不改变挂载作用域**） */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* Select(FName Id);

	/** 结束当前容器作用域 */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UAsceUIBuilder* End();

	// ── 查询 / 结果 ──

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UWidget* FindWidget(FName Id) const;

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UWidget* GetRootWidget() const { return RootWidget; }

	/** 还有多少个容器没 End（>0 = 布局写漏了 End） */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	int32 GetOpenScopeCount() const { return Scopes.Num(); }

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	TArray<FText> GetErrors() const { return Errors; }

	/** 把累积的错误一次性打到日志（widget 构建完调一次） */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	void LogErrors(const FString& Context) const;

private:
	/** 一个挂载作用域（容器 + 还能挂几个孩子） */
	struct FScope
	{
		UWidget* Container = nullptr;
		bool bContentWidget = false;   // Border/SizeBox：单子节点
		int32 ChildCount = 0;
		float Spacing = 0.f;
	};

	void PushScope(UWidget* Container, bool bContentWidget, float Spacing);
	UWidget* CreateWidgetOfClass(UClass* WidgetClass, FName Name);
	bool AttachToCurrent(UWidget* Widget);
	void ApplySlotPadding(UPanelSlot* Slot, EAsceUISpace Padding, float ExtraSpacing);
	void AddError(const FString& Message);

	/** 取样式类并校验基类；不合法退兜底类并记错误 */
	UClass* ResolveChecked(UClass* ConfiguredClass, UClass* RequiredBaseClass, UClass* FallbackClass, const TCHAR* What);

	/** 容器创建 + 挂载 + 压栈 的公共流程 */
	UAsceUIBuilder* BeginContainer(UClass* WidgetClass, FName Id, EAsceUISpace Spacing, bool bSingleChild);

	UPROPERTY(Transient)
	TObjectPtr<UWidgetTree> WidgetTree = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAsceUIStyleSet> StyleSet = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> OwnerWidget = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> RootWidget = nullptr;

	/** 最近创建的控件（供 Padding/MinSize/... 使用） */
	UPROPERTY(Transient)
	TObjectPtr<UWidget> LastWidget = nullptr;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UWidget>> IdToWidget;

	TArray<FScope> Scopes;
	TArray<FText> Errors;
};
