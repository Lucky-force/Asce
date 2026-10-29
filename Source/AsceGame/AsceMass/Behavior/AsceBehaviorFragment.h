// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"

#include "AsceBehaviorFragment.generated.h"

/**
 * **行为/表现的本端运行态**（不复制；两端各算各的）—— **8 字节**。
 *
 * 为什么合成一个 fragment：这些都是"每个实体都有、生命周期相同、都很小"的状态——
 * 拆开会多占列（每列还有 padding/遍历开销），而合起来只要 8 字节。
 * **fragment 的粒度按"archetype 成员是否不同"来分，而不是一个字段一个 fragment。**
 *
 * 为什么这 8 字节是"真省"而不是"打包省"：
 *  - `TimelineStartWorldTime` 用 `float`——因为 `AGameStateBase::GetServerWorldTimeSeconds()` **本身就返回 float**，
 *    存 double 没有精度收益，只是把结构撑到 8 字节对齐 ⇒ 16B；换成 float 才真的落到 8B；
 *  - `LastNotifiedCounter` 收到 **uint16**：它只用来判断"变了没有"，回绕不影响（两次通知之间不可能跨 65536 次动作）；
 *  - `LastNotifiedActionState`（0..5，3 bit）和 `bTimelineActive`（1 bit）打进同一个 `uint8`。
 *
 * 用途（见 `Combat/技能编辑器设计.md` §4.1/§4.2）：
 *  - **时间轴标量**：`TimelineStartWorldTime` + `bTimelineActive`（进入 Casting/Channel 起轴，回其它状态清掉）；
 *  - **动作态通知边沿**：`LastNotified*`（`UAsceStateTreeNotifyProcessor` 判断"要不要给树发事件"）。
 *  - 动画变体选择在表现 cue task（`Asce Play Anim Cue` 的 `Variants` 随机）——
 */
USTRUCT()
struct FAsceBehaviorFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 时间轴起轴时刻（**服务器世界时间**；源本身是 float） */
	UPROPERTY()
	float TimelineStartWorldTime = 0.f;

	/** 上次已通知的动作 `Counter`（截成 uint16：只做"变了没有"的比较） */
	UPROPERTY()
	uint16 LastNotifiedCounter = 0;

	/** bit0..2 = 上次通知的 `EAsceActionState`（0..5）；bit3 = 时间轴是否在跑 */
	UPROPERTY()
	uint8 PackedStateAndFlags = 0;

	// ── 位域访问器（打包细节只在这里）──

	uint8 GetLastNotifiedActionState() const { return static_cast<uint8>(PackedStateAndFlags & 0x07); }
	void SetLastNotifiedActionState(const uint8 State)
	{
		PackedStateAndFlags = static_cast<uint8>((PackedStateAndFlags & ~0x07) | (State & 0x07));
	}

	bool IsTimelineActive() const { return (PackedStateAndFlags & 0x08) != 0; }
	void SetTimelineActive(const bool bActive)
	{
		PackedStateAndFlags = bActive
			? static_cast<uint8>(PackedStateAndFlags | 0x08)
			: static_cast<uint8>(PackedStateAndFlags & ~0x08);
	}
};
