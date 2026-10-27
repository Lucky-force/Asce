// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * 作弊系统的**编译开关**。
 *
 * - **默认全构建都编**（含 Shipping）：组件与执行链路始终存在，运行时靠
 *   **`Asce.Cheat.Enable`**（发货默认 0）+ per-player 权限控制 —— 内部包不必另编一份；
 * - 想彻底从某些构建里剔除：`Build.cs` 里 `PublicDefinitions.Add("ASCE_CHEAT_ENABLED=0")`；
 * - 对外发行包务必在 `DefaultEngine.ini` 的 `[ConsoleVariables]` 里设 `Asce.Cheat.Enable=0`（本来就是默认）。
 *
 * 见 `Cheat/作弊系统设计.md`。
 */
#ifndef ASCE_CHEAT_ENABLED
	#define ASCE_CHEAT_ENABLED 1
#endif
