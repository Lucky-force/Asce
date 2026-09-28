// AsceWorldMapReplicatorActor.h
#pragma once

#include "GameFramework/Info.h"
#include "AsceMass/FlowField/AsceFlowFieldCores.h"
#include "AsceMass/FlowField/AsceLayeredCostField.h"
#include "AsceWorldMapReplicatorActor.generated.h"

class UAsceBakedMapData;
class AAsceCostFieldChunkActor;

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
	virtual void Tick(float DeltaSeconds) override;

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

	/** **有客户端的权威端**（DS / ListenServer）每帧最多把多少格代价变更"标脏"进复制层：
	 *  读档/大范围编辑/批量建墙时把洪峰摊到多帧，避免单帧 delta 超过 bunch 上限。
	 *  Standalone/客户端不走队列（无向外发包问题）。 */
	UPROPERTY(EditAnywhere, Category = "Asce|Map|Replication", meta = (ClampMin = "1"))
	int32 CostFieldReplicatedUpdatesPerFrame = 256;

	/** 是否把代价场（墙/地形）分块复制给客户端。默认**关**：客户端不跑寻路/碰撞/流场，
	 *  不需要这份数据（视野遮挡用的是本地高度场，选择用的是 SpatialGrid）。
	 *  分块复制相关代码完整保留，以后恢复客户端本地模拟时把这里打开即可。 */
	UPROPERTY(EditAnywhere, Category = "Asce|Map|Replication")
	bool bReplicateCostFieldsToClients = false;

	/** 初始化：对齐扇区数，派生并写入 Fog / SpatialGrid（两端都调）；权威端额外 init WorldMapData。 */
	void InitializeMapDimensions();

	/** 编辑器烘焙的初始地图数据（阻挡 + 地形高度）；为空则占位（全通行、高度 0）。 */
	UPROPERTY(EditAnywhere, Category = "Asce|Map")
	TObjectPtr<UAsceBakedMapData> BakedMapData;

	/** 权威端是否已成功应用烘焙数据（InitializeMapDimensions 里设置）。
	 *  Debugger 用它决定是否还要做"占位全通行 + 测试墙"（应用了真实地图就不再占位）。 */
	bool HasBakedDataApplied() const { return bBakedDataApplied; }

	// 权威数据源（**本地数据，不参与复制**）：内容经 `AAsceCostFieldChunkActor` 分块复制到客户端；
	// 客户端这个结构体由本端 `InitializeMapDimensions` 建结构 + 分块回调填充，不应在客户端调 SetCostField。
	FWorldMapData WorldMapData;

	// 代价来源分层（BaseLand / Building / Special），**只在服务端权威端维护**：
	// 客户端不建层，只用分块复制下来的最终值。
	// 不参与网络复制 ⇒ 这里刻意不做 UPROPERTY。
	FAsceLayeredCostField LayeredCostField;

private:
	/** VisionDetailLevel → Scale（整除 GRID_SIZE；越界回退 2）。 */
	int32 GetVisionScale() const;

	/** 向上取整扇区数，使视野网格宽高为 TileCells 的倍数（行字对齐 + tile 完整）。 */
	void AlignSectorCounts(int32& OutSectorX, int32& OutSectorY) const;

	// ── 代价场分块复制（权威端） ──

	/** 按地图尺寸动态切块并 spawn `AAsceCostFieldChunkActor`（每块 ≤2048 条、≥1024 条，目标 actor ≤8），
	 *  并从本地 dense 一次性预填内容；客户端不执行（chunk actor 由复制下发）。 */
	void SpawnCostFieldChunks();

	/** 销毁全部分块 actor（重 init / EndPlay 用） */
	void DestroyCostFieldChunks();

	/** drain 路由：把一格的变更分发到所属分块 actor（同包多次标脏由 FastArray 合并） */
	void RouteCostFieldUpdate(uint32 GridIndex);

	/** 已 spawn 的分块（服务器本地引用；客户端不填） */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AAsceCostFieldChunkActor>> CostFieldChunks;

	/** 每块覆盖的格数（= 每条 pack 格数 × 每块 pack 数；路由用） */
	int32 CostFieldChunkCells = 0;

	/** 权威端已成功应用烘焙数据（见 getter）。 */
	bool bBakedDataApplied = false;
};
