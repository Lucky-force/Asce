// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "AsceMovementOrientationProcessor.generated.h"

struct FMassEntityQuery;

/**
 * 移动Pipeline的最后一步：朝向=速度方向。
 * 现在的实现带"限速转向"：不直接Atan2瞬跳——速度方向会被推挤/格子流场切换
 * 频繁瞬时改变（entity被推向不同格子→不同流场方向→速度瞬变），转向如果瞬跳会"鬼畜"。
 * 改为每帧朝目标朝向以固定角速度旋转（TurnSpeedDegreesPerSecond），视觉平滑；
 * 实际移动方向不变（只修视觉，不动DesiredVelocity/位置——速度照旧）。
 *
 * 对所有entity无差别处理（不区分bRunFullSimulation），因为它只是"读当前Velocity朝向"。
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

	/** 转向限速（度/秒）：朝向向目标方向转动的最大角速度。小=平滑但转入新方向慢；大=更灵敏 */
	UPROPERTY(EditAnywhere, Category = "Orientation")
	float TurnSpeedDegreesPerSecond = 360.f;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
