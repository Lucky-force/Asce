// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTypes.h"
#include "AsceMassMovementSimulationSubsystem.generated.h"

class UMassProcessor;
class UAsceMovementSteeringProcessor;
class UAsceMovementOrientationProcessor;
class UAsceMovementCorrectionProcessor;
class UAsceMassCollisionSolverProcessor;
class UAsceTerrainHeightProcessor;
class UAsceArrivalProcessor;
class UMassApplyMovementProcessor;

/**
 * 每帧手动驱动Movement Pipeline的执行器。
 *
 * 这个项目没有走引擎的"自动Mass处理器链"(没有UMassRuntimeSettings/UMassProcessingComponent)，
 * 而是和UAsceSpatialGridSubsystem一样的模式：用UTickableWorldSubsystem::Tick每帧构造
 * FMassProcessingContext，手动UE::Mass::Executor::Run逐个执行处理器。
 *
 * 执行顺序(手动编排，组依赖在单处理器Run时被忽略)：
 *   Steering → ApplyMovement → CollisionSolver → Arrival → TerrainHeight → Orientation → Correction
 * 对应：
 *   1. UAsceMovementSteeringProcessor：决策(写DesiredVelocity)，AllNetModes
 *   2. UMassApplyMovementProcessor：引擎积分(DesiredVelocity→Velocity→位置)，AllNetModes
 *   3. UAsceMassCollisionSolverProcessor：位置级碰撞求解(圆-圆半量+圆-墙簇全量+速度投影，并行两轮)，AllNetModes
 *   4. UAsceArrivalProcessor：到达判定(纯几何：到点/接触已到达同组单位)+顶飞回退+disruption组重置，AllNetModes
 *   5. UAsceTerrainHeightProcessor：按静态高度场双线性插值贴地(写位置Z)，AllNetModes
 *   6. UAsceMovementOrientationProcessor：朝向=速度方向，AllNetModes
 *   7. UAsceMovementCorrectionProcessor：客户端位置误差修正，仅Client
 *
 * 注意：
 * - 处理器对象用NewObject挂在Subsystem名下(UObject生命周期归World)，不需要手动销毁。
 * - ExecutionFlags在手动Run时不会被自动过滤，这里在加入执行列表时主动按当前网络模式筛选
 *   (同时保留processor自带的EProcessorExecutionFlags判断，双保险)。
 */
UCLASS(Abstract)
class UAsceMassMovementSimulationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UAsceMassMovementSimulationSubsystem();

	//~Begin UTickableWorldSubsystem Interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual TStatId GetStatId() const override;
	//~End UTickableWorldSubsystem Interface

protected:
	virtual void Tick(float DeltaTime) override;

private:
	// 手动Run前按ExecutionFlags判断是否执行（引擎自动链里这步由框架兜底，
	// 手动模式要自己调用ShouldExecute）。
	void ExecuteProcessorIfAllowed(UMassProcessor& Processor, EProcessorExecutionFlags CurrentNetModeFlags, FMassProcessingContext& InContext);

	// World->GetNetMode()的ENetMode枚举值≠EProcessorExecutionFlags位掩码，需要手动映射。
	EProcessorExecutionFlags ConvertNetModeToExecutionFlags(ENetMode NetMode) const;

	// 每帧按序执行的处理器（创建->配置->Run，不进入引擎自动链）
	UPROPERTY(Transient)
	TObjectPtr<UMassProcessor> SteeringProcessor;

	UPROPERTY(Transient)
	TObjectPtr<UMassProcessor> ApplyMovementProcessor;

	UPROPERTY(Transient)
	TObjectPtr<UMassProcessor> CollisionSolverProcessor;

	UPROPERTY(Transient)
	TObjectPtr<UMassProcessor> ArrivalProcessor;

	UPROPERTY(Transient)
	TObjectPtr<UMassProcessor> TerrainHeightProcessor;

	UPROPERTY(Transient)
	TObjectPtr<UMassProcessor> OrientationProcessor;

	UPROPERTY(Transient)
	TObjectPtr<UMassProcessor> CorrectionProcessor;
};
