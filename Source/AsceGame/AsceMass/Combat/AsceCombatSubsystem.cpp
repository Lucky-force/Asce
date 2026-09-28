// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceCombatSubsystem.h"

#include "MassEntitySubsystem.h"
#include "MassProcessor.h"
#include "MassExecutor.h"            // UE::Mass::Executor::Run
#include "MassProcessingContext.h"   // FMassProcessingContext
#include "Engine/World.h"

#include "AsceCombatProcessor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceCombatSubsystem)

void UAsceCombatSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Mass 的 processor 是 UObject：自己创建、自己持有。**必须 CallInitialize 触发 ConfigureQueries**
	// （否则查询匹配不到任何 archetype，`UE::Mass::Executor::Run` 每次都跑到空查询 ⇒ 战斗完全不生效）
	UMassEntitySubsystem* MassEntitySubsystem = InWorld.GetSubsystem<UMassEntitySubsystem>();
	if (!MassEntitySubsystem)
	{
		return;
	}

	CombatProcessor = NewObject<UAsceCombatProcessor>(this);
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	CombatProcessor->CallInitialize(this, EntityManager.AsShared());
}

void UAsceCombatSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// 阶段推进由 `UAsceSimulationSubsystem` 统一驱动，这里什么都不做（保持全局硬顺序）
}

void UAsceCombatSubsystem::RunCombatStage()
{
	UWorld* World = GetWorld();
	// 战斗是服务器权威逻辑：客户端不跑（standalone 的 NetMode 不是 NM_Client ⇒ 会跑）
	if (!World || World->GetNetMode() == NM_Client || !CombatProcessor)
	{
		return;
	}

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (!MassEntitySubsystem)
	{
		return;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	// 与其它处理器一致：本帧的 DeltaTime 走 Mass 的处理上下文（processor 内部不需要自己取）
	FMassProcessingContext Context(EntityManager, World->GetDeltaSeconds());
	UE::Mass::Executor::Run(*CombatProcessor, Context);
}

TStatId UAsceCombatSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAsceCombatSubsystem, STATGROUP_Tickables);
}
