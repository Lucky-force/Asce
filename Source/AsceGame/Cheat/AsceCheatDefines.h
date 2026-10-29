// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * 作弊系统的**编译开关**。
 *
 * - **默认全构建都编**（含 Shipping，发行包也保留作弊指令）：组件与执行链路始终存在，
 *   运行时权限只看组件的 `bCheatAllowed`（主机/单机启动时可由 cvar `Asce.Cheat.Enable` 给自己授权；
 *   主机也可以打开控制台用 `asce.grant` 自授权；客户端由主机 grant）；
 * - 想彻底从某些构建里剔除：`Build.cs` 里 `PublicDefinitions.Add("ASCE_CHEAT_ENABLED=0")`；
 * - 发行包默认保留指令；cvar 只影响主机启动时的自动授权，不影响发行包里的控制台/grant 通道。
 *
 * 见 `Cheat/作弊系统设计.md`。
 */
#ifndef ASCE_CHEAT_ENABLED
	#define ASCE_CHEAT_ENABLED 1
#endif
