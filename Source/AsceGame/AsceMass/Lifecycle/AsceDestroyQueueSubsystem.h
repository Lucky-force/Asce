// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassEntityHandle.h"
#include "Subsystems/WorldSubsystem.h"

#include "AsceDestroyQueueSubsystem.generated.h"

/**
 * 销毁队列（服务端）：死亡现场只"标记 + 入队"，每帧 drain 一部分 —— **限流**，
 * 避免大批死亡一帧把"死亡 RPC / 复制移除 / 销毁"全砸出去。见 `Networking/Mass复制设计.md` §8。
 *
 * 流程：
 *  1. 死亡现场：`FAsceLifecycleFragment::SetDying(true)`（去重 + 别处据此跳过它）+ `Enqueue(Entity)`；
 *  2. 每帧 drain ≤ `MaxPerFrame`：对每个实体
 *     a. 按"该 controller 视野"判定要通知哪些客户端 ⇒ 每个 controller 一条死亡 RPC（NetID 数组）；
 *     b. `SetDestroyRequested(true)` ⇒ 复制侧 Off ⇒ 各 bubble Remove ⇒ 引用计数归 0 ⇒ 销毁阶段销毁。
 *
 * 为什么"先发 RPC、再置 destroyRequested"：RPC 里客户端要**查实体**（读 transform/类型/蓝量…），
 * 所以实体必须还在；RPC 在帧中发出、属性移除在帧末复制 ⇒ RPC 先到（UE 保证 RPC 先于属性复制）。
 */
UCLASS()
class UAsceDestroyQueueSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 死亡现场调用（调用方先置 `FAsceLifecycleFragment::SetDying(true)` 去重）。 */
	void Enqueue(const FMassEntityHandle Entity);

	/** 帧首、阶段 2 之前、权威端门控：drain ≤ `MaxPerFrame`。 */
	void RunDestroyQueueStage();

	int32 GetNumPending() const { return PendingDestroy.Num(); }

	/** 每帧最多处理多少个（限流；可调） */
	static constexpr int32 MaxPerFrame = 256;

private:
	TArray<FMassEntityHandle> PendingDestroy;
};
