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
 *  1. **统一入口 `RequestDestroy`**（战斗死亡现场 / 建筑拆除 / 脚本销毁都走它）：置
 *     `FAsceLifecycleFragment::SetDying(true)`（去重 + 别处据此跳过它）+ `Enqueue(Entity)`；
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
	/** **统一"请求销毁"入口**（非权威端/无效实体返回 false；幂等）：
	 *  置 Dying（停行动 + 表现隐藏；战斗扫描/选择等据此跳过）→ 入队 → 帧首 drain（发死亡通知 + 置
	 *  `DestroyRequested`）→ 复制 Off/各 bubble Remove → 引用计数归 0 → 销毁阶段真正销毁。
	 *  **不**清理移动命令/动作（那是战斗死亡现场自己的事）；没有 `FAsceLifecycleFragment` 的实体
	 *  （模板没挂 `UAsceRTSEntityTrait`）无法跟踪引用计数 ⇒ **立即销毁**兜底并告警一次。 */
	bool RequestDestroy(const FMassEntityHandle Entity);

	/** 死亡现场直接入队（调用方需自行置 Dying、清移动/取消动作；一般用 `RequestDestroy` 即可）。 */
	void Enqueue(const FMassEntityHandle Entity);

	/** 帧首、阶段 2 之前、权威端门控：drain ≤ `MaxPerFrame`。 */
	void RunDestroyQueueStage();

	int32 GetNumPending() const { return PendingDestroy.Num(); }

	/** 每帧最多处理多少个（限流；可调） */
	static constexpr int32 MaxPerFrame = 256;

private:
	TArray<FMassEntityHandle> PendingDestroy;
};
