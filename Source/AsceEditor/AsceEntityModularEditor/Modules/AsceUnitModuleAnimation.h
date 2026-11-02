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

	/** 待机（单条） */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> IdleAnim;

	/** 待机小动作（IdleBreak；数组，随机选） */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TArray<TSoftObjectPtr<UAnimSequence>> IdleBreakAnims;

	/** 死亡（单条） */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> DyingAnim;

	/** 被控制（单条，预留，运行时尚未接） */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> ControlledAnim;

	/** 眩晕（单条，预留，运行时尚未接） */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> StunnedAnim;

	virtual FName GetModuleId() const override;
	virtual FText GetDisplayName() const override;
	virtual void Contribute(const FAsceUnitModuleBuildContext& Context
		, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const override;
	virtual void CollectPreview(FAsceUnitModulePreviewActions& Actions) const override;
	virtual void WriteDefRow(FAsceRTSUnitDef& Row) const override;
	virtual void Validate(TArray<FText>& OutErrors) const override;
};
