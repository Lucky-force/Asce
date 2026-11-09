// AsceDeclarativeWidget.h
#pragma once

#include "CoreMinimal.h"

#include "UI/AsceActivatableWidget.h"

#include "AsceDeclarativeWidget.generated.h"

class UAsceUIBuilder;
class UPanelWidget;
class UWidget;

/**
 * **声明式界面基类**（AS 脚本继承它，只实现 `BuildUI`）。
 *
 * 生命周期：
 * 1. `CreateWidget` → `RebuildWidget()` → 创建 `WidgetTree` + `UAsceUIBuilder` → 调用 `BuildUI(Builder)`
 *    → 取 builder 的根控件写进 `WidgetTree->RootWidget` → `Super::RebuildWidget()`；
 * 2. 之后 `Construct()` 里可以用 `FindWidget(Id)` 拿控件绑事件/数据；
 * 3. 只有第一次构建（不做热重载；改布局重启 PIE）。
 *
 * AS 示例：
 * ```angelscript
 * class UUnitPanel : UAsceDeclarativeWidget
 * {
 *     UFUNCTION(BlueprintOverride)
 *     void BuildUI(UAsceUIBuilder Builder)
 *     {
 *         Builder.VerticalBox(n"Root", EAsceUISpace::M);
 *             Builder.Text(n"Title", "单位信息", EAsceUITextStyleToken::Title);
 *             Builder.ProgressBar(n"HpBar", 0.5, EAsceUIBarStyleToken::Health);
 *         Builder.End();
 *     }
 *
 *     UFUNCTION(BlueprintOverride)
 *     void Construct()
 *     {
 *         if (UProgressBar HpBar = Cast<UProgressBar>(FindWidget(n"HpBar"))) { ... }
 *     }
 * }
 * ```
 */
UCLASS(Abstract, Blueprintable)
class ASCEGAME_API UAsceDeclarativeWidget : public UAsceActivatableWidget
{
	GENERATED_BODY()

public:
	UAsceDeclarativeWidget(const FObjectInitializer& ObjectInitializer);

	/**
	 * 声明布局（AS 用 `BlueprintOverride` 实现）。
	 * 只能在这里创建控件：C++ 侧处于 `RebuildWidget` 阶段，WidgetTree 已就绪。
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Asce|UI")
	void BuildUI(UAsceUIBuilder* Builder);
	virtual void BuildUI_Implementation(UAsceUIBuilder* Builder);

	/** 按布局里声明的 id 取控件（构建之后才有效） */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UWidget* FindWidget(FName Id) const;

	/** 按 id 取多子节点容器（动态 `CreateWidget` 后 `AddChild` 用） */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	UPanelWidget* FindPanel(FName Id) const;

	/** 构建期错误（未知样式/类型不符/重复 id/漏 End…），只读 */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	TArray<FText> GetBuildErrors() const { return BuildErrors; }

	//~UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;

protected:
	/** 构建一次布局（RebuildWidget 里调用；重复调用无副作用） */
	void BuildLayoutOnce();

private:
	UPROPERTY(Transient)
	TObjectPtr<UAsceUIBuilder> LayoutBuilder = nullptr;

	UPROPERTY(Transient)
	TArray<FText> BuildErrors;

	bool bLayoutBuilt = false;
};
