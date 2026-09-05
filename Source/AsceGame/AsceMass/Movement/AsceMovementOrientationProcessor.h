// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "AsceMovementOrientationProcessor.generated.h"

struct FMassEntityQuery;

/**
 * 移动Pipeline的最后一步：朝向直接取速度方向，不做平滑/转向角速度限制——
 * 目前认为朝向不重要，瞬时转身可以接受，等以后觉得视觉上生硬了再加平滑，
 * 到时候只需要改这一个Processor，不影响移动逻辑本身。
 *
 * 对所有entity无差别处理（不区分bRunFullSimulation），因为它只是"读当前Velocity画个朝向"，
 * 不管这个Velocity是本地算的还是网络同步下来的Dead Reckoning值，逻辑都一样。
 *
 * 速度接近0时保留上一帧朝向（不归零/不乱转），避免站定时的抖动。
 */
UCLASS()
class UAsceMovementOrientationProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceMovementOrientationProcessor();

	/** 速度低于这个值就认为"没在移动"，不更新朝向 */
	UPROPERTY(EditAnywhere, Category = "Orientation")
	float MinSpeedToUpdateFacing = 10.f;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
