// AsceUIDeclarativeTypes.h
#pragma once

#include "CoreMinimal.h"

#include "AsceUIDeclarativeTypes.generated.h"

/**
 * 间距 token：由 `UAsceUIStyleSet::SpaceScale` 换算成像素。
 */
UENUM(BlueprintType)
enum class EAsceUISpace : uint8
{
	None,
	XS,
	S,
	M,
	L,
	XL,
};

// ────────────────────────────────────────────
// 样式 token（**一个 token = 一个控件类**）
//
// 视觉（画刷/字体/颜色/边距…）全部在控件资产自己身上配好（WBP 子类 / 类默认值），
// 这里只负责"这个位置用哪个类"，不在代码里调样式数据。
// ────────────────────────────────────────────

/** 面板（`Border` 语义） */
UENUM(BlueprintType)
enum class EAsceUIPanelStyleToken : uint8
{
	None,
	Panel,
	Card,
};

/** 图片（`Image` 语义） */
UENUM(BlueprintType)
enum class EAsceUIImageStyleToken : uint8
{
	None,
	Icon,
	Portrait,
};

/** 文本 */
UENUM(BlueprintType)
enum class EAsceUITextStyleToken : uint8
{
	None,
	Title,
	Body,
	Caption,
};

/** 按钮 */
UENUM(BlueprintType)
enum class EAsceUIButtonStyleToken : uint8
{
	None,
	Primary,
	Secondary,
	Icon,
};

/** 进度条 */
UENUM(BlueprintType)
enum class EAsceUIBarStyleToken : uint8
{
	None,
	Health,
	Mana,
};
