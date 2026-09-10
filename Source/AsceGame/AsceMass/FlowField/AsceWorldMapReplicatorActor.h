// AsceWorldMapReplicatorActor.h
#pragma once

#include "GameFramework/Info.h"
#include "AsceMass/FlowField/AsceFlowFieldCores.h"
#include "AsceWorldMapReplicatorActor.generated.h"

/**
 * 流场寻路代价数据（FWorldMapData）的网络复制权威持有者。
 *
 * - Server（含Standalone）由地图/关卡负责存在；权威端在 BeginPlay 里初始化 WorldMapData，
 *   并把尺寸派发到视野(Fog)与空间网格(SpatialGrid)——是**世界地图尺寸的唯一配置与初始化入口**
 *   （SectorCountX/Y 在此配置，其余尺寸都由它派生）。
 * - Client端由引擎正常的Actor复制流程接收对应实例，之后WorldMapData本身走
 *   FFastArraySerializer增量复制（见FWorldMapData::PostReplicatedChange）；Client不重init数据，
 *   只用相同扇区数初始化本地维度系统（视野/空间网格）。
 * - 不参与渲染/碰撞，纯数据容器，所以继承AInfo；
 * - bAlwaysRelevant=true：全图寻路数据，所有Client都需要，不适合按距离做relevancy筛选。
 */
UCLASS(Blueprintable, BlueprintType)
class AAsceWorldMapReplicatorActor : public AInfo
{
	GENERATED_BODY()

public:
	AAsceWorldMapReplicatorActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ── 地图尺寸配置（唯一事实来源） ──

	/** 扇区数（世界地图尺寸）。32 对齐约束会在初始化时自动向上取整。 */
	UPROPERTY(EditAnywhere, Category = "Asce|Map")
	int32 SectorCountX = 4;

	UPROPERTY(EditAnywhere, Category = "Asce|Map")
	int32 SectorCountY = 4;

	/**
	 * 视野细分等级。Scale（每流场格切成的视野格数）在初始化时按当前 GRID_SIZE 计算：
	 * 取所有能整除 GRID_SIZE、>1 且为偶数的因子升序排列，第 N 个即为该等级的 Scale
	 * （保证 VisionCell = GRID_SIZE/Scale 为整数 cm）。等级越大视野格越细；超范围自动 clamp。
	 */
	UPROPERTY(EditAnywhere, Category = "Asce|Map", meta = (ClampMin = "1", ClampMax = "16"))
	int32 VisionDetailLevel = 1;

	/** 传给空间网格的最小实体半径（其格边长 = 半径*4）。 */
	UPROPERTY(EditAnywhere, Category = "Asce|Map")
	float SpatialMinEntityRadius = 75.f;

	/** 初始化：对齐扇区数，派生并写入 Fog / SpatialGrid（两端都调）；权威端额外 init WorldMapData。 */
	void InitializeMapDimensions();

	// Server权威数据源；Client端这个结构体的内容完全由FastArray复制驱动，
	// 不应该在Client上调用WorldMapData.SetCostField(...)。
	UPROPERTY(Replicated)
	FWorldMapData WorldMapData;

private:
	/** VisionDetailLevel → Scale（整除 GRID_SIZE；越界回退 2）。 */
	int32 GetVisionScale() const;

	/** 向上取整扇区数，使视野网格宽高为 TileCells 的倍数（行字对齐 + tile 完整）。 */
	void AlignSectorCounts(int32& OutSectorX, int32& OutSectorY) const;
};
