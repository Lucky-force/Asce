// AsceWorldMapReplicatorActor.h
#pragma once

#include "GameFramework/Info.h"
#include "AsceMass/FlowField/AsceFlowFieldCores.h"
#include "AsceWorldMapReplicatorActor.generated.h"

/**
 * 流场寻路代价数据（FWorldMapData）的网络复制权威持有者。
 *
 * - Server（含Standalone）由UAscePathFindingSubsystem::OnWorldBeginPlay生成唯一一份；
 * - Client端由引擎正常的Actor复制流程创建对应实例，之后WorldMapData本身走
 *   FFastArraySerializer增量复制（见FWorldMapData::PostReplicatedChange，逻辑早就写好了，
 *   只是之前一直没有Owner替它接上UPROPERTY(Replicated)+DOREPLIFETIME）；
 * - 不参与渲染/碰撞，纯数据容器，所以继承AInfo；
 * - bAlwaysRelevant=true：全图寻路数据，所有Client都需要，不适合按距离做relevancy筛选。
 */
UCLASS()
class AAsceWorldMapReplicatorActor : public AInfo
{
	GENERATED_BODY()

public:
	AAsceWorldMapReplicatorActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Server权威数据源；Client端这个结构体的内容完全由FastArray复制驱动，
	// 不应该在Client上调用WorldMapData.SetCostField(...)。
	UPROPERTY(Replicated)
	FWorldMapData WorldMapData;
};