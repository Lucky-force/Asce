// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "AsceMovementCorrectionProcessor.generated.h"

struct FMassEntityQuery;

/**
 * 客户端专用：消费FAscePositionCorrectionFragment.bCorrectionPending，把本地位置匀速追赶到
 * 服务器权威位置（TargetPosition）——具体是"谁来决定要不要追、追到哪"的逻辑在
 * AsceReplicationPositionHandlers.h::ApplyToFragment（小忽略/中平滑/大Snap三档），
 * 这个Processor只负责"中"这一档的每帧平滑推进，是移动Pipeline最后新增的一步
 * （排在UAsceMovementOrientationProcessor之后或之前都可以，两者互不依赖）。
 *
 * 用FMath::VInterpConstantTo（恒定速度追赶）而不是VInterpTo（指数衰减）：指数衰减理论上
 * 永远追不到终点，还需要额外阈值判断"够近了就算追上"，不如恒定速度直接、可预期，
 * 追赶时长只取决于偏差大小/CorrectionSpeed，不会出现越接近目标越慢的拖尾观感。
 *
 * 只在Client执行（Server是权威，没有"偏差"需要修正）。
 */
UCLASS()
class UAsceMovementCorrectionProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceMovementCorrectionProcessor();

	/** 匀速追赶服务器权威位置的速度（cm/s），越大修正收敛得越快，但过大会不够"平滑" */
	UPROPERTY(EditAnywhere, Category = "Correction")
	float CorrectionSpeed = 600.f;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
