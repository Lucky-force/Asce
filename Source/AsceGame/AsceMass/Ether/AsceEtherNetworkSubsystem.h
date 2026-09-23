// AsceEtherNetworkSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntityHandle.h"
#include "MassEntityTypes.h"
#include "AsceMass/Ether/AsceEtherTypes.h"
#include "AsceMass/Ether/AsceEtherBuildingDef.h"
#include "AsceMass/Ether/AsceEtherGraphCommand.h"
#include "AsceMass/Simulation/AsceTickContext.h"
#include "AsceMass/Fragments/AsceFootprintFragment.h"   // FNode::CachedFootprint
#include "StructUtils/InstancedStruct.h"
#include "AsceEtherNetworkSubsystem.generated.h"

class UAsceEtherProcessCore;
class UAsceSpatialGridSubsystem;

/** 组合段：`Order` = 段内端口的拓扑序（顺序处理）。`Stamp` 是"活跃 epoch"，
 *  **会被多线程用原子交换做去重**（并行阶段）⇒ 只能通过 `ActivateSeg` 修改。 */
struct FAsceEtherSegment
{
	TArray<int32> Order;   // 段内端口（按拓扑序）
	uint32 Stamp = 0;
	bool bActive = false;
};

/** 端口上的小数组（内部布线目标 / 入边来源）：绝大多数端口度数 0~2 ⇒ 内联分配，避免逐端口堆分配。 */
using FAsceEtherPortIdList = TArray<int32, TInlineAllocator<2>>;

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
	uint8 LinkRange = 0;   // 发射距离（格数；0 = 不能连线；只看发射端）

	// —— 实例配置 ——
	EAsceEtherSplitMode SplitMode = EAsceEtherSplitMode::Ordered;
	EAsceEtherTransportType TransportType = EAsceEtherTransportType::Any;
	uint8 Dimension = 0;
	uint8 Direction = 0;      // **世界**方向（= 槽位本地方向 + 建筑朝向；射线/匹配/扫描序都用它）
	uint8 SlotDirection = 0;  // 槽位**本地**方向（身份复用/槽位匹配用；不随建筑旋转变）
	uint8 Offset = 0;         // 该方向边上、从左手侧起顺时针第几格（本地，身份复用用）

	// —— 运行态 ——
	int32 NodeId = INDEX_NONE;
	int32 CellX = 0;
	int32 CellY = 0;
	bool  bActive = false;         // 端口池里是否在用（空闲槽位 = false）
	uint16 Ether = 0;
	int16 SignalType = 0;  // 仅在 Ether > 0 时有意义（0 = 能量）

	// —— 图（后继 / 前驱；外部边是 1:1，直接存对端端口，不再需要全局边表）——
	FAsceEtherPortIdList LinkTargets;  // 内部布线目标（扫描序排序）
	int32 OutTargetPort = INDEX_NONE;  // 外部出边对端（本端口发向谁）
	int32 InSourcePort = INDEX_NONE;   // 外部入边来源（谁发向本端口；反向索引，O(1) 撤销/重扫）

	// —— 入边（pull 模型：只有 `And/Or` 会被推入跳过、由它自己按这个列表取料）——
	// 内容 = 内部布线源 + 唯一一条外部入边源；**始终按 PortId 升序**（pull/合流顺序确定）。
	FAsceEtherPortIdList InSources;
};

/**
 * ether 网络中枢（服务器权威）。**配置（实体定义表）/ 实例（每建筑）/ 行为（ProcessCore）三层分离**：
 *  - 建筑配置：实体定义表（`AsceEntityDefRow.Def = FAsceEtherBuildingDef`，`UAsceEntityDefLibrary` 查）；
 *  - 实例：`FNode`（建筑，含定义数据拷贝）+ `TArray<FAsceEtherPortData>`（全局扁平，按 node 分段）+ 内部布线；
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

	/** 入队"表/数据内容变化"：**只重解析端口类型行**（`ResolvePortTypeConfig`），走一次全量重建。
	 *  ⚠️ 建筑 def（`FNode::DefData`）**不在运行期重解析**：契约是"实体定义表必须在实体创建之前挂载完成"，
	 *  运行期改 def 行**不会生效**（AddNode/Patch 时查不到会 `ensure` 报错但按"空壳"继续）。
	 *  以后要支持编辑器/热重载，需要在这里补"重解析全部节点的 def"。 */
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

	/** 实体 → nodeId（UI 拿到 `EntityNumber` 后定位节点用；找不到返回 INDEX_NONE）。 */
	int32 GetNodeIdByEntity(FMassEntityHandle Entity) const;
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

	/** 内部布线（**拷贝一份**返回；参数是建筑内端口下标）。节点不存在返回 false。
	 *  ⚠️ 不返回内部指针：`Nodes` 是 TArray，扩容 / 节点死亡都会让内部指针失效。 */
	bool GetInternalLinks(int32 NodeId, TArray<FAsceEtherLinkDef>& OutLinks) const;

	/** 改内部布线（建筑内端口下标）。**走命令队列、不立即改数据**：drain 时局部重建
	 *  （`RebuildNodeInternalTopology` + 受影响段，不搬在制品），生效后自动 `++Revision` 通知 UI。
	 *  节点不存在时忽略。别回退到"直写 + `SetBuildingDirty`（全量重建）"。 */
	void SetInternalLinks(int32 NodeId, const TArray<FAsceEtherLinkDef>& Links);

	// ── 建图 / 每 tick 流程（由 `UAsceSimulationSubsystem` 按阶段顺序调用；本系统已关闭自 tick）──

	/** 阶段②：消费结构命令队列（**唯一 drain 点**；调用时空间数据（建筑层）已重建完成）。 */
	void RunCommandDrainStage(const FAsceTickContext& Ctx);

	/** 阶段③：模拟（Pass 1/2/3）。 */
	void RunSimulationStage(const FAsceTickContext& Ctx);

	// ── 调试 ──

	bool GetPortView(int32 PortId, FPortView& OutView) const;
	const FAsceEtherPortIdList* GetPortLinkTargets(int32 PortId) const;

private:
	/** 建筑实例（静态配置 = 创建时从实体定义表解析的 `DefData` 拷贝）。 */
	struct FNode
	{
		FMassEntityHandle Entity;
		FName BuildingId;
		bool bAlive = false;

		/** 建筑定义（从 `AsceEntityDefRow.Def` 拷一份；SDT 挂卸会重建行表，不能存行指针）。 */
		FAsceEtherBuildingDef DefData;
		bool bHasDefData = false;
		bool bDevice = false;                    // DeviceType == Sequential
		bool bLinksInitialized = false;          // 内布线是否已初始化过（玩家清空后不允许默认值回填）
		uint8 CachedDirection = 0;               // 建筑朝向（绕中心顺时针 90° 次数；实体 Transform 上读，`BuildNodePorts` 刷新）
		/** 上次 `BuildNodePorts` 时的 footprint（**摆放局部化 + 拆除时旧位置重扫**要用：
		 *  实体销毁后就取不到 fragment 了，所以必须缓存）。 */
		FAsceGridFootprintFragment CachedFootprint;
		bool bHasCachedFootprint = false;
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

	/** 全量重建（**只允许系统内部调用**：命令应用 / 开发期校验）。外部请用 `PushFullRebuild`。 */
	void RebuildGraphInternal(const FAsceTickContext& Ctx);

	/** 消费命令队列（tick 开头、`RebuildGraphInternal` 之前）：应用结构性修改，必要时触发重建。 */
	void ApplyGraphCommands(const FAsceTickContext& Ctx);

	/** 建/拆节点（命令应用内部用）。 */
	void RemoveNodeInternal(int32 NodeId);

	int32 FindNodeByEntity(FMassEntityHandle Entity) const;

	// ── 结构/配置修订号（`FEtherBuildingFragment::Revision`；**只管结构，不管料流**）──
	//
	// 谁改了"结构/配置"谁 Mark（节点本身 / 受影响的端口所属节点），drain 末尾统一 Flush 写进 fragment。
	// 不在这里直接写：Mark 可能发生在结构变更（observer）期间，那时碰别的实体的 fragment 不安全。
	// 范围：只标"结构 / 端口配置"直接相关的节点；段、器件邻接表这类**派生数据**变化不通知 UI。
	/** 标记某节点（alive 才收） */
	void MarkNodeRevision(int32 NodeId);
	/** 按端口收其所属节点（改边/重扫后调用，覆盖"邻居也变了"） */
	void MarkPortRevisions(const TArray<int32>& PortIds);
	/** 全量重建类（RefreshData / FullRebuild）用：标记所有 alive 节点 */
	void MarkAllNodeRevisions();
	/** 把收集到的节点写进各自实体的 `FEtherBuildingFragment::Revision`（++），然后清空集合 */
	void FlushNodeRevisions();
	int32 FindPortBySlotKey(int32 NodeId, const FAsceEtherSlotKey& Slot) const;

	// ── 增量更新（P1）──

	/** 组合端口？（非器件节点的端口才进段） */
	bool IsCombinationalPort(int32 PortId) const;

	/** 重建某建筑的内布线衍生数据（`LinkTargets` + 目标的 `InSources` + 记录数组）；返回受影响的端口集合。 */
	void RebuildNodeInternalTopology(int32 NodeId, TArray<int32>& OutTouchedPorts);

	/** 外部边写入/撤销：同步维护对端的 `InSourcePort / InSources`（pull 用；按 PortId 升序）。 */
	void SetExternalEdge(int32 SrcPort, int32 DstPort);
	void ClearExternalEdge(int32 SrcPort);

	/** **脱网清理**：把该建筑所有端口从图里摘干净（外部边双向 + 内部布线双向 + 自身字段清零）。
	 *  `BuildNodePorts` 开头会调用（跳过重建时也保证不留幽灵引用）。 */
	void DetachNodeFromGraph(int32 NodeId);

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
	void ValidateIncrementalAgainstFullRebuild(const FAsceTickContext& Ctx);

	/** 端口类型解析（查端口类型表 ⇒ `Op/Throughput/LinkRange`；缺行告警 + 默认值）。
	 *  顺带维护 `MaxPortLinkRange`（反向扫描上限），所以非 const。 */
	void ResolvePortTypeConfig(FAsceEtherPortData& P);

	/** 收集与这些端口相邻的器件节点（用于局部重算它们的"相邻段"表）。 */
	void GatherAdjacentDeviceNodes(const TArray<int32>& TouchedPorts, TArray<int32>& OutDeviceNodes) const;

	// ── 构建/重扫（全量与增量共用）──

	/** 建筑朝向：从实体 Transform（Yaw）取"绕中心顺时针 90° 的次数"（0~3）。 */
	uint8 GetNodeRotation(FMassEntityHandle Entity, const FAsceTickContext& Ctx) const;

	/** 建/刷新某建筑的端口实例（slot → 端口池，按相对几何身份复用；含类型解析与摆放）。 */
	void BuildNodePorts(int32 NodeId, const FAsceTickContext& Ctx);

	/** 确保该建筑的 core（按 `bInstance` 决定 CDO 或实例）并调用 `OnInit`。 */
	void EnsureNodeCore(int32 NodeId);

	/** 射线：从该端口的格出发、按 `Direction` 与 `LinkRange` 找第一个建筑的"收"端口；返回目标 PortId（无则 INDEX_NONE）。 */
	int32 RayScanPort(int32 PortId, const UAsceSpatialGridSubsystem* Grid) const;

	/** 从给定 footprint（逻辑格 AABB）出发，收集"朝本建筑发"的候选来源端口（不含本建筑自己的端口）。 */
	void CollectAdjacentSourcePorts(int32 NodeId, const FAsceGridFootprintFragment& Foot, const UAsceSpatialGridSubsystem* Grid, TArray<int32>& OutSources) const;

	/** 变动后重扫受影响的外部边（本建筑端口的出边 + 会被它挡住/放开的那些来源端口）。
	 *  `ExtraFootprint`（可空）= 调用方额外要一并重扫的"旧 footprint"（`PatchNode(摆放)` 用：
	 *  `BuildNodePorts` 会把缓存刷成新位置，旧位置得由调用方先抓下来传进来）。 */
	void RescanExternalEdgesForNode(int32 NodeId, TArray<int32>& OutTouchedPorts, const FAsceTickContext& Ctx, const FAsceGridFootprintFragment* ExtraFootprint = nullptr);

	/** 把段标成"本拍活跃"（**线程安全**：原子 Stamp 去重 + 写当前线程分片）。 */
	void ActivateSeg(int32 SegId);

	/** 主线程：把分片合并进 `ActiveList`（Pass 1 与 Pass 3 之间调用一次）。 */
	void MergeActiveShards();

	/** 分片数：≥ 线程数 ⇒ 每线程独享一份，不会撞。 */
	int32 GetActiveShardNum() const;

	void PushPort(int32 PortId);
	void ProcessPort(int32 PortId, bool& bOutHasEther);
	/** `And/Or` 端口的求值（pull 模型）：轮到它处理时按 `InSources` 顺序从上游取料（合流、上限=本端口吞吐），
	 *  再统一符号（And：全 + 才 +；Or：有 + 则 +；0 号逻辑透明）；异族/无输入 ⇒ 本拍不动作。 */
	void PullAndEvaluate(int32 PortId);
	uint16 MoveUnits(int32 FromPort, int32 ToPort, uint16 Amount, int16 Type);

	const FAsceEtherBuildingDef* GetNodeDef(int32 NodeId) const;

	// —— 命令队列（结构性修改的唯一入口；每帧 tick 开头 drain）——
	TArray<FInstancedStruct> GraphCommands;

	// —— registry ——
	TArray<FNode> Nodes;
	TArray<int32> FreeIds;
	TMap<FMassEntityHandle, int32> EntityToNode;

	/** 待写修订号的节点（drain 末尾统一 Flush；去重） */
	TSet<int32> PendingRevisionNodes;

	// —— 端口实例（全局扁平 + 空闲池；**持久存在**，按 node 的 SlotPortIds 索引）——
	TArray<FAsceEtherPortData> Ports;
	TArray<int32> FreePortIds;

	/** 全端口射程最大值（反向扫描的上限；重建时精确重算，增量时只增不减——只会多扫一点，无害） */
	int32 MaxPortLinkRange = 0;

	// —— 段 / 顺序 / 映射 ——
	TArray<FAsceEtherSegment> Segments;   // 段池（bActive 标记在用；Order 自带拓扑序）
	TArray<int32> FreeSegIds;
	TArray<int32> PortSegId;              // portId -> segId（-1 = 器件端口/无）。**与 Ports 等长**：Alloc/FreePortId 维护（ClearGraph 整体重置）
	TArray<TArray<int32>> DeviceAdjSegs;  // 器件 nodeId -> 相邻段
	TArray<int32> DeviceNodes;            // 器件（时序）nodeId，**升序**（RebuildGraphInternal 缓存；Pass 1/2 只遍历它）

	// —— 活跃调度（并行安全） ——
	TArray<int32> ActiveList;             // 合并后的主表（Pass 3 遍历它）
	TArray<TArray<int32>> ActiveShards;   // 分片：并行阶段每线程一份（合并后清空）
	int32 ShardAssignCounter = 0;         // 分片索引分配（每线程一个，原子递增）
	uint32 CurrentStamp = 0;

	// —— UObject 引用（GC）——
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAsceEtherProcessCore>> NodeCores;  // 按 nodeId（CDO 或实例）
};
