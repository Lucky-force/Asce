// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class UWorld;
struct FAsceBehaviorFragment;

/**
 * 时间轴标量的读取（`Combat/技能编辑器设计.md` §10.3）。
 *
 * 时钟一律用 **服务器世界时间**（`AGameStateBase::GetServerWorldTimeSeconds()`；客户端也拿得到，
 * 是复制下来的同步时钟）——这样"卡顿"不用特殊处理，且客户端将来可以做速率收敛。
 */
namespace AsceTimeline
{
	/** 服务器世界时间（拿不到 GameState 时退回本地世界时间，单机/编辑器兜底） */
	ASCEGAME_API double GetServerNow(const UWorld& World);

	/** 时间轴当前位置（秒）；`bTimelineActive==0` ⇒ 返回 0 */
	ASCEGAME_API float GetTime(const UWorld& World, const FAsceBehaviorFragment& Behavior);

	/** 时间轴是否已经到 `TargetTime`（`bTimelineActive==0` 视为没到） */
	ASCEGAME_API bool HasReached(const UWorld& World, const FAsceBehaviorFragment& Behavior, float TargetTime);
}
