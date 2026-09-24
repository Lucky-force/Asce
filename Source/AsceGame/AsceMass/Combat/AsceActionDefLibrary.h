// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AsceCombatDef.h"

/**
 * 动作 / 技能定义查询（走 `UAsceDataTableSubsystem`：行结构 `FAsceActionDef` → MDT）。
 *
 * - 表登记方式与其它表一致：在 `UAsceDataTableSettings` 里登记**行结构 = `FAsceActionDef`** 的 MDT；
 *   查询只传行结构，子系统自己解析表在哪（见 `AsceDataTableSubsystem.h`）。
 * - 这里按 **`ActionId`** 查（行名没有强制约定）：先把行表拷出来后线性找。
 *   **行很少，够用**；以后若嫌慢再加"ActionId → 行名"的索引（先这样，不合适再改）。
 * - `ActionId == 0`（攻击，内置动作定义）不走本查询：攻击参数直接在 `FAsceCombatFragment` 里。
 */
struct FAsceActionDefLibrary
{
	/** 按 `ActionId` 查一行；表没登记 / 查不到 → false（不写 OutDef） */
	static bool GetActionDef(int32 ActionId, FAsceActionDef& OutDef);
};
