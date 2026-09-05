
#pragma once

#include "CommonActivatableWidget.h"

#include "AsceActivatableWidget.generated.h"

struct FUIInputConfig;


//输入模式
UENUM(BlueprintType)
enum class EAsceWidgetInputMode : uint8
{
	//默认输入
	Default,
	//游戏中的菜单
	GameAndMenu,
	//游戏
	Game,
	//菜单
	Menu
};

// An activatable widget that automatically drives the desired input config when activated
// 在激活时自动驱动需要的输入配置的可激活widget
UCLASS(Abstract, Blueprintable)
class UAsceActivatableWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UAsceActivatableWidget(const FObjectInitializer& ObjectInitializer);
	
public:
	
	//~UCommonActivatableWidget interface
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
	//~End of UCommonActivatableWidget interface

#if WITH_EDITOR
	virtual void ValidateCompiledWidgetTree(const UWidgetTree& BlueprintWidgetTree, class IWidgetCompilerLog& CompileLog) const override;
#endif
	
protected:
	/** The desired input mode to use while this UI is activated, for example do you want key presses to still reach the game/player controller? */
	UPROPERTY(EditDefaultsOnly, Category = Input)
	EAsceWidgetInputMode InputConfig = EAsceWidgetInputMode::Default;

	/** The desired mouse behavior when the game gets input. */
	UPROPERTY(EditDefaultsOnly, Category = Input)
	EMouseCaptureMode GameMouseCaptureMode = EMouseCaptureMode::CapturePermanently;
	
	UPROPERTY(EditDefaultsOnly, Category = Input)
	bool bShowMouse = false;
};
