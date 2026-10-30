// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "AsceEntityModularEditor/AsceUnitModule.h"

#include "AsceUnitModuleAnimation.generated.h"

class UAnimSequence;
class UAsceAnimSet;

/** 动画：`FAsceAnimPlaybackFragment`（cue 播放状态）+ def 的 `AnimSet`（clip 目录） */
USTRUCT()
struct FAsceUnitModuleAnimation : public FAsceUnitModule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Animation")
	TSoftObjectPtr<UAsceAnimSet> AnimSet;

	/** 待机（可多个变体） */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TArray<TSoftObjectPtr<UAnimSequence>> IdleAnims;

	/** 待机小动作（IdleBreak；Idle 状态里每隔一段随机时间播一个） */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TArray<TSoftObjectPtr<UAnimSequence>> IdleBreakAnims;

	/** 死亡 */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TArray<TSoftObjectPtr<UAnimSequence>> DyingAnims;

	/** 被控制（预留，运行时尚未接） */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TArray<TSoftObjectPtr<UAnimSequence>> ControlledAnims;

	/** 眩晕（预留，运行时尚未接） */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TArray<TSoftObjectPtr<UAnimSequence>> StunnedAnims;

	virtual FName GetModuleId() const override;
	virtual FText GetDisplayName() const override;
	virtual void Contribute(const FAsceUnitModuleBuildContext& Context
		, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const override;
	virtual void CollectPreview(FAsceUnitModulePreviewActions& Actions) const override;
	virtual void WriteDefRow(FAsceRTSUnitDef& Row) const override;
	virtual void Validate(TArray<FText>& OutErrors) const override;
};
