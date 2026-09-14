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
	Super::Tick(DeltaTime);

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