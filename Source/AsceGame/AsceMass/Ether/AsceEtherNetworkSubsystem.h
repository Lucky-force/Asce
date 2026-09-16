// AsceEtherNetworkSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Mass/EntityHandle.h"
#include "MassEntityTypes.h"
#include "AsceMass/Ether/AsceEtherTypes.h"
#include "AsceMass/Ether/AsceEtherBuildingConfig.h"
#include "AsceMass/Ether/AsceEtherGraphCommand.h"
#include "StructUtils/InstancedStruct.h"
#include "AsceEtherNetworkSubsystem.generated.h"

class UAsceEtherProcessCore;

/** 组合段：`Order` = 段内端口的拓扑序（顺序处理）。`Stamp` 是"活跃 epoch"，
 *  **会被多线程用原子交换做去重**（并行阶段）⇒ 只能通过 `ActivateSeg` 修改。 */
struct FAsceEtherSegment
{
	TArray<int32> Order;   // 段内端口（按拓扑序）
	uint32 Stamp = 0;
	bool bActive = false;
};

/**
 * 端口**实例**数据（每个端口一份）。三层拆分：
 * - 端口类型（`PortTypeId` = **端口类型表的行名**）：行里的 `Op` / `ThroughputPerTick` / `LinkRangeCells` 在建图时解析缓存；**玩家可换**；
 * - 实例配置（`SplitMode` / `Dimension` / `Direction` / `Offset`）：玩家可改；
 *   `TransportType` 由 core 在 `OnInit` 设定（玩家不可改）；
 * - 运行态（`Ether` / `SignalType` / 后继 / 入边记录）：每 tick 读写。
 */
struct FAsceEtherPortData
{
	// —— 端口类型（共享数据：端口类型表的行；建图时解析成下面的缓存）——
	FName PortTypeId;
	EAsceEtherPortOp Op = EAsceEtherPortOp::Wire;
	uint16 Throughput = 1;
	int32 LinkRange = 0;   // 发射距离（格数；0 = 不能连线；只看发射端）

	// —— 实例配置 ——
	EAsceEtherSplitMode SplitMode = EAsceEtherSplitMode::Ordered;
	EAsceEtherTransportType TransportType = EAsceEtherTransportType::Any;
	uint8 Dimension = 0;
	uint8 Direction = 0;   // 建筑默认朝向下的方向（AsceEther::EDir）
	uint8 Offset = 0;      // 该方向边上、从左手侧起顺时针第几格

	// —— 运行态 ——
	int32 NodeId = INDEX_NONE;
	int32 CellX = 0;
	int32 CellY = 0;
	bool  bActive = false;         // 端口池里是否在用（空闲槽位 = false）
	uint16 Ether = 0;
	int16 SignalType = 0;  // 仅在 Ether > 0 时有意义（0 = 能量）

	// —— 图（后继 / 前驱；外部边是 1:1，直接存对端端口，不再需要全局边表）——
	TArray<int32> LinkTargets;   // 内部布线目标（已按 §2.9 扫描序排序）
	int32 OutTargetPort = INDEX_NONE;  // 外部出边对端（本端口发向谁）
	int32 InSourcePort = INDEX_NONE;   // 外部入边来源（谁发向本端口；反向索引，O(1) 撤销/重扫）

	// —— And/Or 的入边符号记录 ——
	TArray<int32> InSources;
	TArray<int16> InSigns;
	TArray<uint32> InSignTick;
};

/**
 * ether 网络中枢（服务器权威）。**配置（共享）/ 实例（每建筑）/ 行为（ProcessCore）三层分离**：
 *  - 建筑配置注册表：`BuildingId -> TSubclassOf<UAsceEtherBuildingConfig>`（Trait 建模板时登记）；
 *  - 实例：`FNode`（建筑）+ `TArray<FAsceEtherPortData>`（全局扁平，按 node 分段）+ 内部布线；
 *  - 行为：每个建筑一个 `UAsceEtherProcessCore`（CDO 或实例，见 `bInstance`）；
 *  - 建图：slot → 端口实例 → `Core->OnInit`（定端口角色）→ 用 `TransportType` 定向外部边 → 端口级组合段；
 *  - 每 tick：`Core->Tick`（时序）→ 器件端口推出 → 组合段传播（端口只沿后继推，不看类型）。
 */
UCLASS()
class UAsceEtherNetworkSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 端口视图（调试用）。 */
	struct FPortView
	{
		int32 NodeId = INDEX_NONE;
		uint8 Dir = 0;
		uint8 Dim = 0;
		uint8 Offset = 0;
		EAsceEtherTransportType TransportType = EAsceEtherTransportType::Any;
		int32 CellX = 0;
		int32 CellY = 0;
		uint16 Throughput = 1;
		uint16 Ether = 0;
		int16 SignalType = 0;
		int32 OutTargetPort = INDEX_NONE;
	};

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	static UAsceEtherNetworkSubsystem* Get(const UObject* WorldContextObject);

	// ── 建筑配置注册表（Trait / 调试器登记） ──

	void RegisterBuildingConfig(FName BuildingId, TSubclassOf<UAsceEtherBuildingConfig> ConfigClass);
	/** 取建筑配置（CDO；由 `UPROPERTY` 注册表保活）。 */
	UAsceEtherBuildingConfig* FindBuildingConfig(FName BuildingId) const;

	// ── registry（node = 建筑） ──

	/** 入队"新增建筑"（配置由 registry 按 BuildingId 反查）。 */
	void PushAddNode(FMassEntityHandle Entity, FName BuildingId);

	/** 入队"拆除建筑"。 */
	void PushRemoveNode(FMassEntityHandle Entity);

	/** 入队"改建筑"（换类型 / 移动旋转）。 */
	void PushPatchNode(FMassEntityHandle Entity, const FAsceEtherNodePatch& Patch);

	/** 入队"改内部布线"。 */
	void PushSetNodeLinks(FMassEntityHandle Entity, const TArray<FAsceEtherLinkDef>& Links);

	/** 入队"改端口实例字段"。 */
	void PushPatchPort(FMassEntityHandle Entity, const FAsceEtherSlotKey& Slot, const FAsceEtherPortPatch& Patch);

	/** 入队"表/数据内容变化"。 */
	void PushRefreshData(const UScriptStruct* RowStruct = nullptr);

	/** 入队兜底全量重建。 */
	void PushFullRebuild();

	/** 通用入队（`FInstancedStruct::Make<FAsceEtherCmd_...>()`）。 */
	void PushCommand(FInstancedStruct Command);

	/** 直接注销（内部/调试用；等价于入队 RemoveNode 后立即应用）。 */
	void UnregisterBuilding(int32 NodeId);
	void UnregisterBuildingByEntity(FMassEntityHandle Entity);

	bool IsNodeAlive(int32 NodeId) const;
	FMassEntityHandle GetEntity(int32 NodeId) const;
	/** 器件（时序）；其余为传输器（纯组合）。 */
	bool IsDevice(int32 NodeId) const;

	int32 GetNodeNum() const { return Nodes.Num(); }
	int32 GetPortNum() const { return Ports.Num(); }
	int32 GetNodePortCount(int32 NodeId) const;
	/** slot 序号 -> 全局 PortId（端口持久化：id 稳定，不随别的建筑增删漂移）。 */
	int32 GetNodePortId(int32 NodeId, int32 SlotIndex) const;
	UAsceEtherProcessCore* GetProcessCore(int32 NodeId) const;

	/** 标脏：入队一次全量重建（兼容旧调用）。 */
	void SetBuildingDirty() { PushFullRebuild(); }

	// ── 端口访问（供 ProcessCore / 玩家逻辑；LocalIndex = 建筑内端口下标 = slot 下标） ──

	FAsceEtherPortData* GetPort(int32 NodeId, int32 LocalIndex);
	const FAsceEtherPortData* GetPort(int32 NodeId, int32 LocalIndex) const;
	int32 FindPortByTransport(int32 NodeId, EAsceEtherTransportType Want) const;

	uint16 GetPortEther(int32 NodeId, int32 LocalIndex) const;
	int16 GetPortSignalType(int32 NodeId, int32 LocalIndex) const;
	void SetPort(int32 NodeId, int32 LocalIndex, uint16 Ether, int16 Type);
	void ClearPort(int32 NodeId, int32 LocalIndex);

	/** 用 0 号 ether（能量）把 Type 重铸到该端口：`1 份 = abs(Type)` 份能量；不足 ⇒ 不产出、不消耗。 */
	bool MintFromEnergy(int32 NodeId, int32 LocalIndex, int16 Type);

	/** 内部布线（建筑内端口下标；改完请 `SetBuildingDirty`）。 */
	const TArray<FAsceEtherLinkDef>* GetInternalLinks(int32 NodeId) const;
	void SetInternalLinks(int32 NodeId, const TArray<FAsceEtherLinkDef>& Links);

	// ── 建图 / 每 tick ──

	void RebuildGraph();
	void RunEtherTick();

	// ── 调试 ──

	bool GetPortView(int32 PortId, FPortView& OutView) const;
	const TArray<int32>* GetPortLinkTargets(int32 PortId) const;

private:
	/** 建筑实例（静态配置在 `RebuildGraph` 里按 `ConfigClass` 解析）。 */
	struct FNode
	{
		FMassEntityHandle Entity;
		FName BuildingId;
		bool bAlive = false;

		UAsceEtherBuildingConfig* Config = nullptr;  // CDO（注册表保活；RebuildGraph 直接用，不再查）
		bool bDevice = false;                    // DeviceType == Sequential
		uint8 CachedDirection = 0;               // 建筑旋转（预留：暂恒 0）
		/** slot 序号 -> 全局 PortId（**端口持久化**：按相对几何身份复用，id 不随别的建筑增删而漂）。 */
		TArray<int32> SlotPortIds;
		TArray<FAsceEtherLinkDef> InternalLinks; // 实例内布线（玩家可改）
	};

	int32 AllocNodeId();
	/** 端口池：分配/回收（空闲槽位复用；端口持久存在，id 稳定）。 */
	int32 AllocPortId();
	void  FreePortId(int32 PortId);
	/** 段池：分配/回收（空闲槽位复用；局部重算时只动受影响的段）。 */
	int32 AllocSegId();
	void  FreeSegId(int32 SegId);

	void ClearGraph();

	/** 消费命令队列（tick 开头、`RebuildGraph` 之前）：应用结构性修改，必要时触发重建。 */
	void ApplyGraphCommands();

	/** 建/拆节点（命令应用内部用）。 */
	void RemoveNodeInternal(int32 NodeId);

	int32 FindNodeByEntity(FMassEntityHandle Entity) const;
	int32 FindPortBySlotKey(int32 NodeId, const FAsceEtherSlotKey& Slot) const;

	// ── 增量更新（P1）──

	/** 组合端口？（非器件节点的端口才进段） */
	bool IsCombinationalPort(int32 PortId) const;

	/** 重建某建筑的内布线衍生数据（`LinkTargets` + 目标的 `InSources` + 记录数组）；返回受影响的端口集合。 */
	void RebuildNodeInternalTopology(int32 NodeId, TArray<int32>& OutTouchedPorts);

	/** **局部段重算**：只以"受影响端口所在的旧段 + 受影响端口"为范围重跑连通分量与拓扑序（合并/拆分自然处理）。
	 *  `OutAllAffectedPorts`（可空）回传实际参与重算的全部端口（供调用方整理器件邻接表）；
	 *  `ExtraSegIds`（可空）额外纳入的旧段（例如"整栋建筑被拆掉"时它的段要先把端口收进范围再回收）。 */
	void RecomputeSegmentsForPorts(const TArray<int32>& TouchedPorts, TArray<int32>* OutAllAffectedPorts = nullptr, const TArray<int32>* ExtraSegIds = nullptr);

	/** 局部重算这些器件节点的"相邻段"表。 */
	void RecomputeDeviceAdjSegsForNodes(const TArray<int32>& DeviceNodes);

	/** 活跃表重建：把"仍有料"的段重新入列（局部更新后用，代价只有一次线性扫描）。 */
	void RefreshInitialActiveSegments();

	// ── 开发期校验：局部更新结果 vs 全量重建 ──

	/** 开发期开关：局部更新后跑一次全量重建做 diff（不一致则报错，并用全量结果兜底）。 */
	bool bValidateIncremental = false;

	/** 图摘要（端口出/入目标 + 内部目标 + 入边源、段（按端口集合签名）、器件相邻段），排序后可比对。 */
	void BuildGraphDigest(TArray<FString>& OutDigest) const;

	/** 校验：先取局部结果摘要 → 全量重建 → 再取摘要 → 对比（不一致打日志）。 */
	void ValidateIncrementalAgainstFullRebuild();

	/** 端口类型解析（查端口类型表 ⇒ `Op/Throughput/LinkRange`；缺行告警 + 默认值）。 */
	void ResolvePortTypeConfig(FAsceEtherPortData& P) const;

	/** 收集与这些端口相邻的器件节点（用于局部重算它们的"相邻段"表）。 */
	void GatherAdjacentDeviceNodes(const TArray<int32>& TouchedPorts, TArray<int32>& OutDeviceNodes) const;

	// ── 构建/重扫（全量与增量共用）──

	/** 建/刷新某建筑的端口实例（slot → 端口池，按相对几何身份复用；含类型解析与摆放）。 */
	void BuildNodePorts(int32 NodeId);

	/** 确保该建筑的 core（按 `bInstance` 决定 CDO 或实例）并调用 `OnInit`。 */
	void EnsureNodeCore(int32 NodeId);

	/** 射线：从该端口的格出发、按 `Direction` 与 `LinkRange` 找第一个建筑的"收"端口；返回目标 PortId（无则 INDEX_NONE）。 */
	int32 RayScanPort(int32 PortId) const;

	/** 变动后重扫受影响的外部边（本建筑端口的出边 + 会被它挡住/放开的那些来源端口）。 */
	void RescanExternalEdgesForNode(int32 NodeId, TArray<int32>& OutTouchedPorts);

	// ── 占用索引（扁平网格；射线 O(1) 查格，增量增删只要改一小片） ──

	/** 依据地图尺寸准备 `GridToNode`（尺寸一致就复用）。 */
	void EnsureGridIndex();

	/** 格 -> nodeId（越界/空地 = INDEX_NONE）。 */
	int32 GetNodeAtCell(int32 X, int32 Y) const;

	/** 把某建筑的占用格写进/清出网格。 */
	void MarkNodeCells(int32 NodeId, bool bMark);

	int32 GridW = 0;
	int32 GridH = 0;
	TArray<int32> GridToNode;   // cell(y*GridW+x) -> nodeId（INDEX_NONE = 空）

	/** 把段标成"本拍活跃"（**线程安全**：原子 Stamp 去重 + 写当前线程分片）。 */
	void ActivateSeg(int32 SegId);

	/** 主线程：把分片合并进 `ActiveList`（Pass 1 与 Pass 3 之间调用一次）。 */
	void MergeActiveShards();

	/** 分片数：≥ 线程数 ⇒ 每线程独享一份，不会撞。 */
	int32 GetActiveShardNum() const;

	void PushPort(int32 PortId);
	void ProcessPort(int32 PortId, bool& bOutHasEther);
	void EvaluateOp(int32 PortId);
	uint16 MoveUnits(int32 FromPort, int32 ToPort, uint16 Amount, int16 Type);

	const UAsceEtherBuildingConfig* GetConfigCDO(int32 NodeId) const;

	// —— 建筑配置注册表（UPROPERTY ⇒ 属性系统保活，不需要额外 keepalive）——
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UAsceEtherBuildingConfig>> BuildingConfigRegistry;

	// —— 命令队列（结构性修改的唯一入口；每帧 tick 开头 drain）——
	TArray<FInstancedStruct> GraphCommands;

	// —— registry ——
	TArray<FNode> Nodes;
	TArray<int32> FreeIds;
	TMap<FMassEntityHandle, int32> EntityToNode;

	// —— 端口实例（全局扁平 + 空闲池；**持久存在**，按 node 的 SlotPortIds 索引）——
	TArray<FAsceEtherPortData> Ports;
	TArray<int32> FreePortIds;

	// —— 段 / 顺序 / 映射 ——
	TArray<FAsceEtherSegment> Segments;   // 段池（bActive 标记在用；Order 自带拓扑序）
	TArray<int32> FreeSegIds;
	TArray<int32> PortSegId;              // portId -> segId（-1 = 器件端口/无）
	TArray<TArray<int32>> DeviceAdjSegs;  // 器件 nodeId -> 相邻段
	TArray<int32> DeviceNodes;            // 器件（时序）nodeId，**升序**（RebuildGraph 缓存；Pass 1/2 只遍历它）

	// —— 活跃调度（并行安全） ——
	TArray<int32> ActiveList;             // 合并后的主表（Pass 3 遍历它）
	TArray<TArray<int32>> ActiveShards;   // 分片：并行阶段每线程一份（合并后清空）
	int32 ShardAssignCounter = 0;         // 分片索引分配（每线程一个，原子递增）
	uint32 CurrentStamp = 0;

	// —— UObject 引用（GC）——
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAsceEtherProcessCore>> NodeCores;  // 按 nodeId（CDO 或实例）
};
