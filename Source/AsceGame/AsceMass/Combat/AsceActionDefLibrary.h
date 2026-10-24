// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AsceCombatDef.h"

/**
 * 动作 / 技能定义查询（走 `UAsceDataTableSubsystem`：行结构 `FAsceActionDef` → MDT）。
 *
 * - 表登记方式与其它表一致：在 `UAsceDataTableSettings` 里登记**行结构 = `FAsceActionDef`** 的 MDT；
 * - 两种查法：
 *   - **按 `ActionId`**（fragment/复制里存的是 id）——首次调用建一次索引，之后 **O(1)**；
 *   - **按行名**（单位 def 的 `AttackAction` / 技能槽位引用都是行名）——同样走同一份索引。
 * - **`ActionId == 0` = 内置攻击（没有行）**：仍返回 false。攻击改成"一行 Auto 动作"之后，
 *   攻击行会有自己的 `ActionId`（>0），单位 def 用行名指向它。
 * - 索引**不做热更**（表在运行时不变）；编辑器里改了表要重进 PIE。
 */
struct FAsceActionDefLibrary
{
	/** 按 `ActionId` 查一行（O(1)）；表没登记 / 查不到 → false（不写 OutDef） */
	static bool GetActionDef(int32 ActionId, FAsceActionDef& OutDef);

	/** 按**行名**查一行（O(1)）；查不到 → false */
	static bool GetActionDefByRowName(FName RowName, FAsceActionDef& OutDef);

	/** 清索引（改表后想立即生效时手动调；正常运行不需要） */
	static void InvalidateCache();
};
