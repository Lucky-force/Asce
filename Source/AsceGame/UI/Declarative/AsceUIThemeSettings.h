// AsceUIThemeSettings.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Layout/Margin.h"
#include "Templates/SubclassOf.h"

#include "AsceUIDeclarativeTypes.h"

#include "AsceUIThemeSettings.generated.h"

class UBorder;
class UImage;
class UProgressBar;
class UTextBlock;
class UWidget;

/**
 * **样式表：一个 token = 一个控件类**。
 *
 * 视觉全部配在控件资产里（WBP 子类 / C++ 类默认值），这里只决定"这个位置用哪个类"：
 * - Text/Title → `WBP_TextTitle`（`UTextBlock` 派生，字体/颜色在 WBP 里）
 * - Button/Primary → `WBP_ButtonPrimary`（`UButton` 或 CommonButton 派生）
 * - …
 *
 * 需要特殊外观 ⇒ 加一条 token + 一个控件资产；**不在代码里调样式数据**。
 * 项目设置：Project Settings → Asce → UI Styles。
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Asce UI Styles"))
class ASCEGAME_API UAsceUIStyleSet : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAsceUIStyleSet();

	/** 面板样式（`Border` 语义）→ 控件类（必须 `UBorder` 派生） */
	UPROPERTY(config, EditAnywhere, Category = "Styles")
	TMap<EAsceUIPanelStyleToken, TSoftClassPtr<UBorder>> PanelStyles;

	/** 图片样式 → 控件类（必须 `UImage` 派生） */
	UPROPERTY(config, EditAnywhere, Category = "Styles")
	TMap<EAsceUIImageStyleToken, TSoftClassPtr<UImage>> ImageStyles;

	/** 文本样式 → 控件类（必须 `UTextBlock` 派生，例如 `UCommonTextBlock` 或 WBP 子类） */
	UPROPERTY(config, EditAnywhere, Category = "Styles")
	TMap<EAsceUITextStyleToken, TSoftClassPtr<UTextBlock>> TextStyles;

	/** 按钮样式 → 控件类（`UButton` 或 `UCommonButtonBase` 派生） */
	UPROPERTY(config, EditAnywhere, Category = "Styles")
	TMap<EAsceUIButtonStyleToken, TSoftClassPtr<UWidget>> ButtonStyles;

	/** 进度条样式 → 控件类（必须 `UProgressBar` 派生） */
	UPROPERTY(config, EditAnywhere, Category = "Styles")
	TMap<EAsceUIBarStyleToken, TSoftClassPtr<UProgressBar>> BarStyles;

	/** 间距 token → 像素 */
	UPROPERTY(config, EditAnywhere, Category = "Spacing")
	TMap<EAsceUISpace, float> SpaceScale;

	/** 取样式类（未配置/加载失败返回 nullptr，由 builder 兜底到默认类） */
	UClass* FindPanelClass(EAsceUIPanelStyleToken Token) const;
	UClass* FindImageClass(EAsceUIImageStyleToken Token) const;
	UClass* FindTextClass(EAsceUITextStyleToken Token) const;
	UClass* FindButtonClass(EAsceUIButtonStyleToken Token) const;
	UClass* FindBarClass(EAsceUIBarStyleToken Token) const;

	float GetSpace(EAsceUISpace Space) const;
	FMargin GetMargin(EAsceUISpace Space) const;

	virtual FName GetCategoryName() const override { return FName(TEXT("Asce")); }
};
