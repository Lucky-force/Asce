// Fill out your copyright notice in the Description page of Project Settings.

#include "AscePathFindingSubsystem.h"
#include "AsceWorldMapReplicatorActor.h"

void UAscePathFindingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FallbackEmptyWorldMapData.InitWorldMapData(1, 1);
	PathFinder = NewObject<UPathFinder>(this);
	PathFinder->SetWorldMapData(&FallbackEmptyWorldMapData);
}

void UAscePathFindingSubsystem::Deinitialize()
{
	PathFinder = nullptr;

	WorldMapReplicator = nullptr;
	
	Super::Deinitialize();
}

UAscePathFindingSubsystem* UAscePathFindingSubsystem::Get(const UObject* WorldContextObject)
{
	if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		return UWorld::GetSubsystem<UAscePathFindingSubsystem>(World);
	}
	return nullptr;
}

void UAscePathFindingSubsystem::Tick(float DeltaTime)
{
	// 自 tick 已停用：本 tick 体已挪成 `RunFlowFieldStage`，由 `UAsceSimulationSubsystem` 按阶段顺序调用
	Super::Tick(DeltaTime);
}

void UAscePathFindingSubsystem::RunFlowFieldStage(const FAsceTickContext& Ctx)
{
	// 客户端不跑寻路/流场：不预测、不碰撞，PathFinder 在客户端没有消费者（位置由服务器同步驱动）。
	// 以后恢复客户端本地模拟时把这个早退删掉即可（寻路代码完整保留）。
	const UWorld* World = Ctx.World ? Ctx.World : GetWorld();
	if (World && World->GetNetMode() == NM_Client)
	{
		return;
	}

	// 每帧给 goal LOS 失效队列一个预算（与实体寻路调用解耦，即使本帧没有实体也推进重算）。
	constexpr int32 GoalLOSRefreshBudgetPerFrame = 10;
	if (PathFinder)
	{
		PathFinder->ProcessGoalLOSRefreshQueue(GoalLOSRefreshBudgetPerFrame);
	}
}

TStatId UAscePathFindingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAscePathFindingSubsystem, STATGROUP_Tickables);
}

void UAscePathFindingSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// WorldMapReplicatorActor 现在改为在关卡里摆放并配置（SectorCountX/Y 等在 Actor 上设置），
	// 它自己会在 BeginPlay 里 InitializeMapDimensions() 并调 RegisterWorldMapReplicator。
	// 这里不再 spawn。Client 端靠关卡里的同名 Actor + 复制接收数据。
}

void UAscePathFindingSubsystem::RegisterWorldMapReplicator(AAsceWorldMapReplicatorActor* InReplicator)
{
	WorldMapReplicator = InReplicator;
	if (PathFinder && InReplicator)
	{
		PathFinder->SetWorldMapData(&InReplicator->WorldMapData);
	}
}

void UAscePathFindingSubsystem::UnregisterWorldMapReplicator(AAsceWorldMapReplicatorActor* InReplicator)
{
	if (WorldMapReplicator == InReplicator)
	{
		WorldMapReplicator = nullptr;
		if (PathFinder)
		{
			PathFinder->SetWorldMapData(&FallbackEmptyWorldMapData);
		}
	}
}

FWorldMapData* UAscePathFindingSubsystem::GetWorldMapDataMutableForAuthority()
{
	if (!ensureMsgf(WorldMapReplicator, TEXT("WorldMapReplicator尚未就绪，可能是在OnWorldBeginPlay之前调用的")))
	{
		return nullptr;
	}
	checkf(WorldMapReplicator->HasAuthority(), TEXT("只有Server能改代价数据，Client应该等FastArray复制下来"));
	return &WorldMapReplicator->WorldMapData;
}

FAsceLayeredCostField* UAscePathFindingSubsystem::GetLayeredCostFieldMutableForAuthority()
{
	if (!ensureMsgf(WorldMapReplicator, TEXT("WorldMapReplicator尚未就绪，可能是在OnWorldBeginPlay之前调用的")))
	{
		return nullptr;
	}
	checkf(WorldMapReplicator->HasAuthority(), TEXT("只有Server能改代价分层，Client只用复制下来的最终CostFields"));
	return &WorldMapReplicator->LayeredCostField;
}

uint64 UAscePathFindingSubsystem::AddSpecialCostOp(int32 GridX, int32 GridY, EAsceCostOp Op, uint32 ContributionID)
{
	AAsceWorldMapReplicatorActor* Replicator = WorldMapReplicator;
	if (!Replicator || !Replicator->HasAuthority())
	{
		// 客户端不应该调（它只用复制下来的最终 CostFields）
		ensureMsgf(false, TEXT("代价分层只能在Server(权威端)修改"));
		return FAsceLayeredCostField::InvalidHandle;
	}

	FWorldMapData& WorldMapData = Replicator->WorldMapData;
	FAsceLayeredCostField& LayeredCostField = Replicator->LayeredCostField;
	const int32 GridCountX = WorldMapData.GetGridCountX();
	const int32 GridCountY = WorldMapData.GetGridCountY();
	if (GridX < 0 || GridY < 0 || GridX >= GridCountX || GridY >= GridCountY)
	{
		UE_LOG(LogFlowField, Warning,
			TEXT("[SpecialCost] 越界：(%d,%d) 地图 %dx%d"), GridX, GridY, GridCountX, GridCountY);
		return FAsceLayeredCostField::InvalidHandle;
	}

	const uint32 GridIndex = WorldMapData.GridLocationToGridIndex(FIntVector2(GridX, GridY));
	const uint64 Handle = LayeredCostField.AddSpecialOp(GridIndex, Op, ContributionID);
	if (Handle != FAsceLayeredCostField::InvalidHandle)
	{
		// 逐格组合写回：只有"组合结果与现值不同"才真的 SetCostField（⇒ 才触发复制/扇区脏标记）
		LayeredCostField.FlushCell(GridIndex, WorldMapData);
	}
	return Handle;
}

void UAscePathFindingSubsystem::RemoveSpecialCostOp(uint64 Handle)
{
	if (Handle == FAsceLayeredCostField::InvalidHandle)
	{
		return;
	}

	AAsceWorldMapReplicatorActor* Replicator = WorldMapReplicator;
	if (!Replicator || !Replicator->HasAuthority())
	{
		ensureMsgf(false, TEXT("代价分层只能在Server(权威端)修改"));
		return;
	}

	FWorldMapData& WorldMapData = Replicator->WorldMapData;
	// Handle = (GridIndex << 32) | ID ⇒ 由 Handle 直接得到格子，无需反查
	const uint32 GridIndex = static_cast<uint32>(Handle >> 32);
	Replicator->LayeredCostField.RemoveSpecialOp(Handle);
	Replicator->LayeredCostField.FlushCell(GridIndex, WorldMapData);
}

void UAscePathFindingSubsystem::EnqueuePlanGroupReset(FMergedPathPlanHandle PlanHandle)
{
	if (PlanHandle.IsValid())
	{
		PendingPlanGroupResets.Add(PlanHandle.RawID);
	}
}

void UAscePathFindingSubsystem::DrainPendingPlanGroupResets(TSet<uint32>& OutPlanRawIDs)
{
	OutPlanRawIDs = MoveTemp(PendingPlanGroupResets);
	PendingPlanGroupResets.Reset();
}
