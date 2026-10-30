// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"

class UAnimSequence;
struct FMassStateTreeExecutionContext;
class UAsceAnimSet;
struct FAsceAnimPlaybackFragment;
struct FAsceEntityDefRow;
struct FAsceRTSUnitDef;

/**
 * **动画 cue 的公共小工具**（各个"播放动画"task 共用）。
 *
 * - 动画数据在**实体定义表**（`FAsceRTSUnitDef` 的 `*Anims` 数组）；帧段在 `UAsceAnimSet::Clips`；
 * - 任务只负责"选定一个序列 → 解析成帧段 → 写 `FAsceAnimPlaybackFragment::Cue*`"；
 *   真正的逐帧推进 + 落地到 ISM 是 `AsceCombatVisualProcessor` 的事；
 * - 专用服务器不写 cue（没有表现）。
 */
namespace AsceAnimCue
{
	/** 取该实体的单位定义（**拷贝**到 OutDef；没有 / 不是 `FAsceRTSUnitDef` 返回 false） */
	bool GetUnitDef(const FMassStateTreeExecutionContext& MassContext, FAsceRTSUnitDef& OutDef);

	/** 从数组里随机选一个有效序列（全空返回 nullptr） */
	const TSoftObjectPtr<UAnimSequence>* PickRandom(const TArray<TSoftObjectPtr<UAnimSequence>>& Animations);

	/**
	 * 写 cue：解析 `AnimSet` 里该序列的帧段，写进 `Playback`（客户端）。返回是否成功。
	 * - `bLoop`：循环（Idle/Move）或一次性（Attack）；
	 * - `PlayRate`：播放速率（1.0 = 按烘焙帧率）。
	 */
	bool PlaySequence(FAsceAnimPlaybackFragment& Playback, const UAsceAnimSet* AnimSet
		, const TSoftObjectPtr<UAnimSequence>& Sequence, bool bLoop, float PlayRate);

	/** 当前 `Playback` 里正在播的 cue 对应的序列（用来判断"是否需要重播"） */
	bool IsPlayingSequence(const FAsceAnimPlaybackFragment& Playback, const UAsceAnimSet* AnimSet
		, const TSoftObjectPtr<UAnimSequence>& Sequence);
}
