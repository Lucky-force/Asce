// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassEntityTypes.h"
#include "Math/Vector2D.h"

struct FMassEntityHandle;
class UPathFinder;

/**
 * "解析合法性+按NetID寻址"和"真正把Order写进Fragment"是两件事——前者Server/Client各有各的
 * 寻址方式（Server要NetID反查EntityHandle，Client本地selection本来就是EntityHandle，不需要反查），
 * 后者(真正调PathFinder+写Fragment)两边逻辑应该完全一致，所以抽成这个共享静态函数，
 * Server的RPC handler和Client的本地预测入口都调用同一份实现，不允许出现"两边各写一遍、
 * 以后改一边忘了改另一边"的情况。
 */
struct FAsceMoveOrderExecutor
{
	/**
	 * 对一批已经确认有效的entity执行"合并寻路请求 + 写入FAsceMoveOrderFragment"。
	 *
	 * 调用前提（调用方自己负责，这里不重复做防御性判断）：
	 * - ValidHandles/EntityWorldLocations2D一一对应、等长；
	 * - 每个Handle在传入的EntityManager里都合法，且都带有FAsceMoveOrderFragment。
	 *
	 * 行为：整批entity共享同一个目标，只发起一次RunMergeablePathRequest（真正用上"Mergeable"），
	 * 然后逐个entity释放旧Handle（如果有）、认领新Handle、写入TargetLocation/bHasOrder/OrderSequence。
	 */
	static void ApplyMergedMoveOrder(
		FMassEntityManager& EntityManager,
		UPathFinder& PathFinder,
		const TArray<FMassEntityHandle>& ValidHandles,
		const TArray<FVector2D>& EntityWorldLocations2D,
		const FVector2D& GoalWorldLocation,
		float AcceptanceRadiusSquared = 2500.f,
		int32 OrderSequence = 0);
};
