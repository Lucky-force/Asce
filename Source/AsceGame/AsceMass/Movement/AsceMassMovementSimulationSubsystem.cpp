// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMassMovementSimulationSubsystem.h"

#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "MassProcessingTypes.h"
#include "MassProcessor.h"
#include "AsceMass/Movement/AsceMovementSteeringProcessor.h"
#include "AsceMass/Movement/AsceMovementOrientationProcessor.h"
#include "AsceMass/Movement/AsceMovementCorrectionProcessor.h"
#include "AsceMass/Movement/AsceMassCollisionSolverProcessor.h"
#include "AsceMass/Movement/AsceArrivalProcessor.h"
#include "AsceMass/Movement/AsceTerrainHeightProcessor.h"
#include "Movement/MassMovementProcessors.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceMassMovementSimulationSubsystem)

UAsceMassMovementSimulationSubsystem::UAsceMassMovementSimulationSubsystem()
{
}

bool UAsceMassMovementSimulationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// 只在 GameWorld / PIE 创建，不在编辑器预览世界创建
	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	return World->IsGameWorld() || World->IsPlayInEditor();
}

TStatId UAsceMassMovementSimulationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAsceMassMovementSimulationSubsystem, STATGROUP_Tickables);
}

void UAsceMassMovementSimulationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// UMassEntitySubsystem此时保证存在（WorldSubsystem依赖顺序由MassEntity模块保证），
	// 拿到EntityManager, 用NewObject创建处理器后调用CallInitialize触发ConfigureQueries——
	// 这一步不做的话查询永远不会被配置，Executor::Run会跑到空查询。
	UMassEntitySubsystem* EntitySubsystem = InWorld.GetSubsystem<UMassEntitySubsystem>();
	if (!EntitySubsystem)
	{
		return;
	}

	UObject* Outer = this;
	SteeringProcessor = NewObject<UAsceMovementSteeringProcessor>(Outer);
	ApplyMovementProcessor = NewObject<UMassApplyMovementProcessor>(Outer);
	CollisionSolverProcessor = NewObject<UAsceMassCollisionSolverProcessor>(Outer);
	ArrivalProcessor = NewObject<UAsceArrivalProcessor>(Outer);
	TerrainHeightProcessor = NewObject<UAsceTerrainHeightProcessor>(Outer);
	OrientationProcessor = NewObject<UAsceMovementOrientationProcessor>(Outer);
	CorrectionProcessor = NewObject<UAsceMovementCorrectionProcessor>(Outer);

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	SteeringProcessor->CallInitialize(Outer, EntityManager.AsShared());
	ApplyMovementProcessor->CallInitialize(Outer, EntityManager.AsShared());
	CollisionSolverProcessor->CallInitialize(Outer, EntityManager.AsShared());
	ArrivalProcessor->CallInitialize(Outer, EntityManager.AsShared());
	TerrainHeightProcessor->CallInitialize(Outer, EntityManager.AsShared());
	OrientationProcessor->CallInitialize(Outer, EntityManager.AsShared());
	CorrectionProcessor->CallInitialize(Outer, EntityManager.AsShared());
}

void UAsceMassMovementSimulationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!SteeringProcessor || !ApplyMovementProcessor || !CollisionSolverProcessor || !ArrivalProcessor || !TerrainHeightProcessor || !OrientationProcessor || !CorrectionProcessor)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (!EntitySubsystem)
	{
		return;
	}

	// Pipeline顺序手动编排（组依赖在Executor::Run单处理器时被忽略）：
	// Steering(决策) → ApplyMovement(积分) → CollisionSolver(碰撞) → Arrival(到达判定)
	// → TerrainHeight(贴地) → Orientation(朝向) → Correction(位置修正)
	FMassProcessingContext ProcessingContext(EntitySubsystem->GetMutableEntityManager(), DeltaTime);
	EProcessorExecutionFlags CurrentNetModeFlags = ConvertNetModeToExecutionFlags(World->GetNetMode());
	ExecuteProcessorIfAllowed(*SteeringProcessor, CurrentNetModeFlags, ProcessingContext);
	ExecuteProcessorIfAllowed(*ApplyMovementProcessor, CurrentNetModeFlags, ProcessingContext);
	ExecuteProcessorIfAllowed(*CollisionSolverProcessor, CurrentNetModeFlags, ProcessingContext);
	ExecuteProcessorIfAllowed(*ArrivalProcessor, CurrentNetModeFlags, ProcessingContext);
	ExecuteProcessorIfAllowed(*TerrainHeightProcessor, CurrentNetModeFlags, ProcessingContext);
	ExecuteProcessorIfAllowed(*OrientationProcessor, CurrentNetModeFlags, ProcessingContext);
	ExecuteProcessorIfAllowed(*CorrectionProcessor, CurrentNetModeFlags, ProcessingContext);
}

EProcessorExecutionFlags UAsceMassMovementSimulationSubsystem::ConvertNetModeToExecutionFlags(ENetMode NetMode) const
{
	switch (NetMode)
	{
		case NM_Standalone:
			return EProcessorExecutionFlags::Standalone;
		case NM_Client:
			return EProcessorExecutionFlags::Client;
		case NM_DedicatedServer:
		case NM_ListenServer:
			return EProcessorExecutionFlags::Server;
		default:
			return EProcessorExecutionFlags::None;
	}
}

void UAsceMassMovementSimulationSubsystem::ExecuteProcessorIfAllowed(UMassProcessor& Processor, EProcessorExecutionFlags CurrentNetModeFlags, FMassProcessingContext& InContext)
{
	// 引擎自动链里这步由MassProcessingPhaseManager兜底（BuildProcessingPhaseFilter），
	// 手动Run时必须自己判一下，否则Client的CorrectionProcessor会在Server上也跑，
	// Server端的FAscePositionCorrectionFragment虽然挂载但无意义（见AsceRTSUnitTrait注释）。
	if (!Processor.ShouldExecute(CurrentNetModeFlags))
	{
		return;
	}

	UE::Mass::Executor::Run(Processor, InContext);
}
