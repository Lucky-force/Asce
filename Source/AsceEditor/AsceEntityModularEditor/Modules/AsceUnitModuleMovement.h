// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "AsceEntityModularEditor/AsceUnitModule.h"

#include "AsceUnitModuleMovement.generated.h"

/** 移动：运动学/指令/校正 + Tuning + 可移动 tag；自带 Radius/Height（值优先用 Volume 的） */
USTRUCT()
struct FAsceUnitModuleMovement : public FAsceUnitModule
{
	GENERATED_BODY()

	/** 基础移动速度（cm/s）：最大速度 = 2×基础；奔跑阈值 = 1.7×基础 */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "1.0"))
	float BaseMoveSpeed = 150.f;


	/** 普通移动（可多个变体；预览用，运行时由 cue/树读取后接入） */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> MoveAnim;

	/** 快速奔跑 */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> RunAnim;

	/** 受伤后移动 */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> InjuredMoveAnim;

	virtual FName GetModuleId() const override;
	virtual FText GetDisplayName() const override;
	virtual void Contribute(const FAsceUnitModuleBuildContext& Context
		, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const override;
	virtual void WriteDefRow(FAsceRTSUnitDef& Row) const override;
	virtual void CollectPreview(FAsceUnitModulePreviewActions& Actions) const override;
};
