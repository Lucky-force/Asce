// Fill out your copyright notice in the Description page of Project Settings.

#include "AscePathFindingSubsystem.h"
#include "AsceWorldMapReplicatorActor.h"
#if WITH_EDITORONLY_DATA
#include "AsceFlowFieldDebugger.h"
#endif

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

void UAscePathFindingSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 只有Server（含Standalone）负责生成这个权威Actor；Client端等它随网络复制自然出现，
	// 出现时会在AAsceWorldMapReplicatorActor::BeginPlay里调RegisterWorldMapReplicator，
	// 不需要Client自己Spawn。
	if (InWorld.GetNetMode() != NM_Client)
	{
		InWorld.SpawnActor<AAsceWorldMapReplicatorActor>();
	}
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

void UAscePathFindingSubsystem::RegisterFlowFieldDebugger(AAsceFlowFieldDebugger* InDebugger)
{
#if WITH_EDITORONLY_DATA
	FlowFieldDebugger = InDebugger;
	if (PathFinder && InDebugger)
	{
		// 换网到Debugger的WorldMapData：双方共享同一个PathFinder实例+同一份地图数据，
		// Debugger上编辑墙/代价的结果直接对运行时寻路生效。
		PathFinder->SetWorldMapData(InDebugger->GetWorldMapDataForDebugger());
	}
#endif
}

void UAscePathFindingSubsystem::UnregisterFlowFieldDebugger(AAsceFlowFieldDebugger* InDebugger)
{
#if WITH_EDITORONLY_DATA
	if (FlowFieldDebugger == InDebugger)
	{
		FlowFieldDebugger = nullptr;
		if (PathFinder)
		{
			// 还原：优先回Replicator的数据，没有就彻底回Fallback
			if (WorldMapReplicator)
			{
				PathFinder->SetWorldMapData(&WorldMapReplicator->WorldMapData);
			}
			else
			{
				PathFinder->SetWorldMapData(&FallbackEmptyWorldMapData);
			}
		}
	}
#endif
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