// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeProcessors.h"
// 引擎那个头会 `#define UE_API MASSAIBEHAVIOR_API` 且不 undef ⇒ 立刻清掉，避免污染后续包含
#undef UE_API

#include "AsceStateTreeProcessors.generated.h"

/**
 * MassAI 的行为树 processor 默认只跑 `Standalone | Server`（`UE::MassStateTree::ExecutionFlags`，
 * 因为 MassAI 是 AI 模块、AI 服务器权威）。这里三个**薄子类**把 `ExecutionFlags` 改成 `AllNetModes`，
 * 让**客户端也跑**（表现 task）——详见 `Combat/技能编辑器设计.md` §3.3。
 *
 * ⚠️ **必须同时在 `DefaultMass.ini` 里关掉引擎默认的两个**（activation / destructor）：
 *    听服世界的 exec flags = `Client|Server`，否则引擎版(Standalone|Server) 和我们的(Client)
 *    会在听服上**同时命中** ⇒ 实例数据重复分配。
 *
 * 分工：
 * - `UAsceStateTreeProcessor`：每帧 tick（动态 processor，走 `UMassBehaviorSettings::DynamicStateTreeProcessorClass`）；
 * - `UAsceStateTreeActivationProcessor`：分配实例数据 + 发激活信号；
 * - `UAsceStateTreeFragmentDestructor`：移除 fragment 时释放实例数据。
 */

UCLASS()
class ASCEGAME_API UAsceStateTreeProcessor : public UMassStateTreeProcessor
{
	GENERATED_BODY()

public:
	UAsceStateTreeProcessor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

UCLASS()
class ASCEGAME_API UAsceStateTreeActivationProcessor : public UMassStateTreeActivationProcessor
{
	GENERATED_BODY()

public:
	UAsceStateTreeActivationProcessor();
};

UCLASS()
class ASCEGAME_API UAsceStateTreeFragmentDestructor : public UMassStateTreeFragmentDestructor
{
	GENERATED_BODY()

public:
	UAsceStateTreeFragmentDestructor();
};
