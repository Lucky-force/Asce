// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MassExternalSubsystemTraits.h"
#include "AsceMass/FlowField/AsceFlowFieldCores.h"
#include "AsceMass/FlowField/AscePathFinder.h"
#include "AscePathFindingSubsystem.generated.h"

class AAsceWorldMapReplicatorActor;
class AAsceFlowFieldDebugger;

/**
 * 流场寻路的运行时持有者：Client和Server各自独立创建一份UPathFinder（跟MassEntityManager一样，
 * 两边是完全独立的实例，互不感知对方的内存地址/Handle）。
 *
 * 之前UPathFinder+FWorldMapData只有AAsceFlowFieldDebugger这个`#if WITH_EDITORONLY_DATA`包起来的
 * 调试Actor在用——那个类打包之后根本不存在，所以运行时（包括PIE以外的真实联机场景）
 * 一直没有地方能拿到UPathFinder实例。这个Subsystem就是补上这个洞：Movement模块的
 * Processor（AsceMovementSteeringProcessor）通过AddSubsystemRequirement<UAscePathFindingSubsystem>
 * 拿到它，调GetPathFinder()查方向。
 *
 * WorldMapData的网络同步已经接通：权威数据由AAsceWorldMapReplicatorActor持有，
 * Server端在OnWorldBeginPlay里生成唯一一份，Client端靠引擎正常的Actor复制流程接收对应实例，
 * 数据内容本身走FWorldMapData自带的FastArraySerializer增量复制（见FWorldMapData::
 * PostReplicatedChange）。Actor就位（Register/UnregisterWorldMapReplicator）之前，
 * PathFinder指向FallbackEmptyWorldMapData兜底，避免读到未初始化内存。
 *
 * 遗留TODO：关卡静态障碍物/地形分析结果具体怎么灌入WorldMapData的初始内容还没做，
 * 见AsceWorldMapReplicatorActor::BeginPlay里的占位InitWorldMapData(4, 4)。
 */
UCLASS()
class UAscePathFindingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	UPathFinder* GetPathFinder() const { return PathFinder; }
	FWorldMapData* GetWorldMapDataMutableForAuthority();
	void RegisterWorldMapReplicator(AAsceWorldMapReplicatorActor* InReplicator);
	void UnregisterWorldMapReplicator(AAsceWorldMapReplicatorActor* InReplicator);

	/** Debugger开局自注册：把PathFinder换网到Debugger的WorldMapData上（两者共享同一实例），
	 * 世界数据编辑（画墙等）结果直接对运行时寻路生效，不需要复制一份。*/
	void RegisterFlowFieldDebugger(AAsceFlowFieldDebugger* InDebugger);
	void UnregisterFlowFieldDebugger(AAsceFlowFieldDebugger* InDebugger);

	static UAscePathFindingSubsystem* Get(const UObject* WorldContextObject);

protected:
	UPROPERTY(Transient)
	FWorldMapData FallbackEmptyWorldMapData;

	UPROPERTY(Transient)
	TObjectPtr<AAsceWorldMapReplicatorActor> WorldMapReplicator;

	UPROPERTY(Transient)
	TObjectPtr<AAsceFlowFieldDebugger> FlowFieldDebugger;

	UPROPERTY(Transient)
	TObjectPtr<UPathFinder> PathFinder;
};

template<>
struct TMassExternalSubsystemTraits<UAscePathFindingSubsystem>
{
	enum
	{
		// UPathFinder内部有可变的scratch状态（PortalSearchScratch/CurrentSearchStamp/FlowFieldCache），
		// 不是线程安全的，只能在GameThread上跑，不能跟其它Processor的ParallelFor重叠访问。
		GameThreadOnly = true,
	};
};
