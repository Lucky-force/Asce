// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Cheat/AsceCheatTypes.h"

#include "AsceCheatAction.generated.h"

/**
 * 一条作弊指令（**数据**）：只声明 名字 / 分类 / 参数表 + 实现一个 `Execute`。
 *
 * - **加一个新作弊 = 只写一个 UCLASS**：`UAsceCheatLibrary` 用 `GetDerivedClasses` 自动发现 ?
 *   控制台（`asce.cheat <Name> k=v ...`）和 UI（枚举分类/参数/默认值）**零改动可用**；
 * - 具体实现在各模式的 Ops 里（如 `Actions/RTS/AsceRTSCheatOps`），动作本身不判权限（权限在组件里）；
 * - 参数值统一存在 `FAsceCheatActionArg::ArgContent`（字符串），用 `GetFloat()/GetName()/...` 转成实际值。
 *
 * 各模式从本类派生自己的基类（如 `UAsceRTSCheatAction`），这样列表/执行可以按模式过滤。
 *
 * 见 `Cheat/作弊系统设计.md`。
 */
UCLASS(Abstract)
class ASCEGAME_API UAsceCheatAction : public UObject
{
	GENERATED_BODY()

public:
	/** 唯一名字（控制台命令用，如 `SpawnUnits`） */
	virtual FName GetActionName() const PURE_VIRTUAL(UAsceCheatAction::GetActionName, return NAME_None;);

	/** UI 显示名（默认 = ActionName） */
	virtual FText GetDisplayName() const { return FText::FromName(GetActionName()); }

	/** UI 分组（如 `生成` / `删除` / `查询`） */
	virtual FText GetCategory() const { return FText::GetEmpty(); }

	/** 参数表（**默认值写在 `ArgContent` 里**；顺序不保证，参数一律 `Key=Value`） */
	virtual TMap<FName, FAsceCheatActionArg> BuildArgs() const { return TMap<FName, FAsceCheatActionArg>(); }

	/** 执行：`Args` 已经是"带默认 + 输入覆盖 + 校验过"的结果；失败写 `OutError`，成功写 `OutSummary` */
	virtual bool Execute(const FAsceCheatContext& Context, const TMap<FName, FAsceCheatActionArg>& Args
		, FString& OutSummary, FString& OutError) const PURE_VIRTUAL(UAsceCheatAction::Execute, return false;);
};
