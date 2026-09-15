// AsceEtherNetworkSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Mass/EntityHandle.h"
#include "MassEntityTypes.h"
#include "AsceMass/Ether/AsceEtherTypes.h"
#include "AsceMass/Ether/AsceEtherBuildingData.h"
#include "AsceEtherNetworkSubsystem.generated.h"

/**
 * 外部连接边（端口级，1:1）。FromPort/ToPort 是**全局 PortId**。
 */
struct FAsceEtherEdge
{
	int32 FromPort = INDEX_NONE;
	int32 ToPort = INDEX_NONE;
};

/** 内部布线（同一建筑的两个端口之间；可合流/分流）。 */
struct FAsceEtherLink
{
	int32 FromPort = INDEX_NONE;
	int32 ToPort = INDEX_NONE;
};

/**
 * 组合段：ProcessOrder 里的一段连续区间（一个组合连通分量）。
 * Stamp 用于活跃列表去重（epoch）。
 */
struct FAsceEtherSegment
{
	int32 Start = 0;
	int32 Count = 0;
	uint32 Stamp = 0;
};

/**
 * ether 网络中枢（服务器权威）：
 *  - BuildingData 注册表：Trait 在模板构建时登记 `BuildingDefId -> UAsceEtherBuildingData`；
 *  - registry：建筑 nodeId（数组下标 + 空闲池复用）→ 全局**端口区间** [PortStart, +PortCount)；
 *  - 图：**端口级**（node = port）；外部边与内部布线都是**变长边表**（不再是定长/位掩码）；
 *  - 运行态（端口缓冲 + 布线结果）在**本类**，不在 fragment；器件内部状态才进 fragment（§3.2）；
 *  - 段与顺序：组合端口按段连续排进 ProcessOrder；器件端口不入表，靠 DeviceAdjSegs 找相邻段；
 *  - 每 tick：器件采样 → 注入活跃段 → 处理活跃段（段内拓扑序）。
 */
UCLASS()
class UAsceEtherNetworkSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	static UAsceEtherNetworkSubsystem* Get(const UObject* WorldContextObject);

	// ── 建筑类型 DA 注册表（Trait 在 BuildTemplate 时登记） ──

	void RegisterBuildingData(FName DefId, const UAsceEtherBuildingData* Data);
	const UAsceEtherBuildingData* FindBuildingData(FName DefId) const;

	// ── registry（node = 建筑） ──

	/** 注册一个 ether 建筑，返回 nodeId（数组下标，含复用）；配置由 BuildingDefId 查注册表。 */
	int32 RegisterBuilding(FMassEntityHandle Entity, FName BuildingDefId);

	/** 注销建筑（结构变化，触发重建）。 */
	void UnregisterBuilding(int32 NodeId);

	/** 按实体注销（Observer Remove 调用）。 */
	void UnregisterBuildingByEntity(FMassEntityHandle Entity);

	bool IsNodeAlive(int32 NodeId) const { return Nodes.IsValidIndex(NodeId) && Nodes[NodeId].bAlive; }
	FMassEntityHandle GetEntity(int32 NodeId) const { return IsNodeAlive(NodeId) ? Nodes[NodeId].Entity : FMassEntityHandle(); }
	/** 器件（时序单元）= 非传输器；传输器 = 纯组合。 */
	bool IsDevice(int32 NodeId) const { return IsNodeAlive(NodeId) && Nodes[NodeId].bDevice; }

	// ── 端口查询（调试/外部访问） ──

	int32 GetPortNum() const { return Ports.Num(); }
	int32 GetNodePortStart(int32 NodeId) const { return IsNodeAlive(NodeId) ? Nodes[NodeId].PortStart : INDEX_NONE; }
	int32 GetNodePortCount(int32 NodeId) const { return IsNodeAlive(NodeId) ? Nodes[NodeId].PortCount : 0; }

	// ── 建图 / 重建（结构变化时，服务器） ──

	/** 重算：端口运行态 → 内部布线/外部边 → 组合段 → ProcessOrder → 映射（会清空活跃表）。 */
	void RebuildGraph();

	// ── 每 tick ──

	/** 跑一个 ether tick：器件采样 → 注入活跃段 → 处理活跃段。 */
	void RunEtherTick();

private:
	/** 建筑节点（静态配置在 RebuildGraph 时从 DA 解析缓存）。 */
	struct FNode
	{
		FMassEntityHandle Entity;
		FName BuildingDefId;
		bool  bAlive = false;

		// —— 以下在 RebuildGraph 里从 UAsceEtherBuildingData 解析 ——
		uint8 DeviceOp = 0;             // EAsceEtherDeviceOp（器件）
		bool  bDevice = false;          // 器件（时序）；传输器 = false（纯组合）
		int32 PortStart = 0;            // 全局 PortId 区间起点
		int32 PortCount = 0;
		TArray<int32> DeviceInputPorts;  // 角色表（全局 PortId；器件）
		TArray<int32> DeviceOutputPorts; // 角色表（全局 PortId；器件）
		TArray<FAsceEtherRecipeItem> RecipeInputs;
		FAsceEtherRecipeItem RecipeOutput;
	};

	/** 端口运行态（全局扁平、变长）：静态配置 + 缓冲 + 布线结果。 */
	struct FPort
	{
		int32  NodeId = INDEX_NONE;
		uint8  Dir = 0;
		uint8  Dim = 0;
		int32  CellX = 0;
		int32  CellY = 0;
		uint16 Throughput = 1;          // 每 tick 吞吐 = 该端口缓冲上限
		bool   bIsInput = false;
		EAsceEtherPortOp Op = EAsceEtherPortOp::Wire;
		EAsceEtherSplitMode SplitMode = EAsceEtherSplitMode::Ordered;

		// —— 运行态 ——
		uint16 Ether = 0;
		uint16 SignalType = 0;          // 0 = 空

		// 内部布线结果：本端口 -> 目标端口（已按分流顺序排序）
		TArray<int32> LinkTargets;
		// 外部出边（本端口 -> 对端入端口；1:1，最多一条）
		int32 OutEdge = INDEX_NONE;
	};

	int32 AllocNodeId();
	void ClearGraph();

	/** epoch 去重入活跃表；返回是否新加入。 */
	bool ActivateSeg(int32 SegId);

	/** 处理一个组合端口（内部布线分流 → 外部推送）；bOutHasEther 输出本段是否还有料。 */
	void ProcessPort(int32 PortId, bool& bOutHasEther);

	// —— DA 注册表 ——
	TMap<FName, TWeakObjectPtr<UAsceEtherBuildingData>> BuildingDataRegistry;

	// —— registry ——
	TArray<FNode> Nodes;                          // index = nodeId
	TArray<int32> FreeIds;                        // 空闲 nodeId 池
	TMap<FMassEntityHandle, int32> EntityToNode;  // 实体 -> nodeId

	// —— 端口运行态（全局扁平，按 node 连续分段） ——
	TArray<FPort> Ports;

	// —— 图（端口级） ——
	TArray<FAsceEtherEdge> Edges;   // 外部边
	TArray<FAsceEtherLink> Links;   // 内部布线

	// —— 段 / 顺序 / 映射 ——
	TArray<int32> ProcessOrder;           // 组合 portId，按段连续排列
	TArray<FAsceEtherSegment> Segments;   // 段区间
	TArray<int32> PortSegId;              // portId -> segId（组合端口；-1 = 器件端口/无）
	TArray<TArray<int32>> DeviceAdjSegs;  // 器件 nodeId -> 相邻段 id 列表

	// —— 活跃调度 ——
	TArray<int32> ActiveList;   // 持久活跃段（每段唯一）
	uint32 CurrentStamp = 0;
};
