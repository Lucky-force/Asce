// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MassEntityHandle.h"
#include "StructUtils/StructView.h"

class UWorld;
struct FMassEntityManager;

/**
 * 给实体的行为树（Mass × StateTree）**发事件**的统一入口。
 *
 * 链路（见 `Combat/技能编辑器设计.md` §4）：
 *  1. 找到实体的树实例（`FMassStateTreeInstanceFragment` → `UMassStateTreeSubsystem` 的实例数据）；
 *  2. 事件塞进该实例的**事件队列**（`FStateTreeEventQueue::SendEvent`）；
 *  3. 给实体发 `UE::Mass::Signals::NewStateTreeTaskRequired` 信号 ⇒ 让它**这一帧/下一 tick** 跑到该事件
 *     （不发信号的话事件会一直躺在队列里，等下次外部信号才被处理）。
 *
 * 典型用法：系统（指令下发 / 战斗扫描）改完 `FAsceActionFragment` 后发一条事件，
 * 树的转移条件用 `RequiredEvent = <Tag>` 命中即可切换外层状态（Idle/Move/Attack/Casting）。
 */
namespace AsceStateTree
{
	/** @return false = 该实体没有可用的树实例（没配 StateTree / 还没激活 / 本端没有这棵树） */
	ASCEGAME_API bool SendEventToEntity(
		UWorld& World,
		FMassEntityManager& EntityManager,
		const FMassEntityHandle Entity,
		const FGameplayTag& Tag,
		const FConstStructView Payload = FConstStructView(),
		const FName Origin = FName());
}
