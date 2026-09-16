// AsceEtherDebugger.h
#pragma once

#include "GameFramework/Actor.h"
#include "MassArchetypeTypes.h"
#include "MassEntityHandle.h"
#include "AsceEtherDebugger.generated.h"

class UAsceEtherBuildingData;

/** 调试放置：建筑类型资产 + 逻辑格坐标（与 footprint/流场同一格网，GRID_SIZE=100cm） */
USTRUCT()
struct FAsceEtherDebugPlacement
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Ether")
	TObjectPtr<UAsceEtherBuildingData> Data;

	UPROPERTY(EditAnywhere, Category = "Ether")
	FIntPoint Cell = FIntPoint(0, 0);
};

/**
 * Ether 系统调试器（临时开发用；正式建筑放置流程接入后删除/停用）：
 *  - BeginPlay（仅服务器）按 Placements 生成建筑实体：
 *    FTransformFragment + FAsceGridFootprintFragment（origin=格坐标）+ FEtherBuildingFragment（+器件状态）；
 *    实体创建会触发 ether 注册 Add 观察者，建筑自动进入 UAsceEtherNetworkSubsystem；
 *  - 每帧绘制：占格、端口（方向箭头 + 维度/类型/数量）、外部连线、内部布线、器件内部状态。
 */
UCLASS()
class AAsceEtherDebugger : public AActor
{
	GENERATED_BODY()

public:
	AAsceEtherDebugger();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, Category = "Ether")
	TArray<FAsceEtherDebugPlacement> Placements;

	UPROPERTY(EditAnywhere, Category = "Ether")
	bool bDrawDebug = true;

private:
	void SpawnAndRegister();
	void UnregisterAndDestroy();
	void DrawDebugState();

	TArray<FMassEntityHandle> SpawnedEntities;
};
