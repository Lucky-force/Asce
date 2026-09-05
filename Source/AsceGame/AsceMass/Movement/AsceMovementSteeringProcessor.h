// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "AsceMovementSteeringProcessor.generated.h"

struct FMassEntityQuery;

/**
 * 移动Pipeline的"决策"环节：负责把FAsceMoveOrderFragment（要去哪）变成
 * FMassDesiredMovementFragment.DesiredVelocity（这一帧该往哪走多快）。
 * 之后交给引擎自带的UMassApplyForceProcessor/UMassApplyMovementProcessor
 * （ExecutionOrder分别是ApplyForces/Movement组，本Processor挂在更早的Avoidance组，
 * 天然排在它们前面）去把DesiredVelocity变成真正的Velocity+位置积分，不需要我们自己写。
 *
 * 只处理FAsceMovementSimStateFragment.bRunFullSimulation==true的entity——
 * 为false的entity完全跳过（DesiredVelocity维持网络同步下来的值不变，靠引擎自带的
 * ApplyMovementProcessor自然做Dead Reckoning外推，见AsceMovementFragments.h的注释）。
 *
 * 本Processor只负责"意图"（想往哪走），单位间的碰撞/穿模修正不在这里——
 * 见UAsceMovementSeparationProcessor（每个实体每帧都做位置级推开，和有没有订单无关）。
 */
UCLASS()
class UAsceMovementSteeringProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceMovementSteeringProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
