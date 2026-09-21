// AsceEtherNetworkSubsystem.cpp
#include "AsceEtherNetworkSubsystem.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Algo/BinarySearch.h"
#include "HAL/PlatformAtomics.h"
#include "HAL/PlatformMisc.h"
#include "Async/ParallelFor.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassCommonFragments.h"   // FTransformFragment（读建筑朝向）
#include "AsceMass/Fragments/AsceFootprintFragment.h"
#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "DataTableSystem/AsceDataTableSubsystem.h"
#include "AsceMass/Ether/AsceEtherCores.h"
#include "AsceMass/Ether/AsceEtherPortTypeTable.h"
#include "AsceMass/SpatialGrid/AsceSpatialGridSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceEtherNetworkSubsystem)

namespace
{
	/** 并行阈值：数量小于它就退回串行（避免任务调度开销）。 */
	constexpr int32 EtherParallelThreshold = 8;

	/** 端口"等效发"：`Output` 恒发；`Input` 恒收；`Any` = 没有内部布线目标（传输器）。 */
	FORCEINLINE bool IsEmittingPort(const FAsceEtherPortData& P)
	{
		switch (P.TransportType)
		{
			case EAsceEtherTransportType::Input:  return false;
			case EAsceEtherTransportType::Output: return true;
			default:                              return P.LinkTargets.Num() == 0;
		}
	}

	/** 端口"等效收"：与 `IsEmittingPort` 互补。 */
	FORCEINLINE bool IsAcceptingPort(const FAsceEtherPortData& P)
	{
		switch (P.TransportType)
		{
			case EAsceEtherTransportType::Input:  return true;
			case EAsceEtherTransportType::Output: return false;
			default:                              return P.LinkTargets.Num() > 0;
		}
	}

	/** 建筑内的格偏移 -> 端口所在格。slot 规则：该方向边上、从**左手侧**起顺时针第 Offset 格。
	 *  - `Origin`/`SizeX`/`SizeY` 是 footprint 的**最终 AABB**（旋转已折进去）；
	 *  - `Direction`/`Offset` 是**建筑本地**槽位定义；
	 *  - `CachedDirection` = 建筑绕中心**顺时针** 90° 的次数（0~3）。 */
	FIntPoint SlotToCell(const FIntPoint& Origin, int32 SizeX, int32 SizeY, uint8 Direction, uint8 Offset, uint8 CachedDirection)
	{
		// 最终 AABB 尺寸 → 未旋转尺寸（奇数旋转时长宽对调）
		const bool bSwap = (CachedDirection & 1) != 0;
		const int32 SX = FMath::Max(1, bSwap ? SizeY : SizeX);
		const int32 SY = FMath::Max(1, bSwap ? SizeX : SizeY);

		// 1) 本地格（相对"未旋转 footprint 的最小角"）
		FIntPoint Local(0, 0);
		switch (static_cast<AsceEther::EDir>(Direction & 3))
		{
			case AsceEther::EDir::Left:  Local = FIntPoint(0, Offset); break;
			case AsceEther::EDir::Up:    Local = FIntPoint(Offset, SY - 1); break;
			case AsceEther::EDir::Right: Local = FIntPoint(SX - 1, SY - 1 - Offset); break;
			case AsceEther::EDir::Down:  Local = FIntPoint(SX - 1 - Offset, 0); break;
			default: break;
		}

		// 2) 本地 → 世界（绕中心顺时针 90°；结果仍落在最终 AABB 的局部坐标里）
		switch (CachedDirection & 3)
		{
			case 1: Local = FIntPoint(Local.Y, SX - 1 - Local.X); break;
			case 2: Local = FIntPoint(SX - 1 - Local.X, SY - 1 - Local.Y); break;
			case 3: Local = FIntPoint(SY - 1 - Local.Y, Local.X); break;
			default: break;
		}

		return Origin + Local;
	}
}

void UAsceEtherNetworkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 活跃段分片：≥ 线程数（并行阶段每线程独享一份）
	ActiveShards.SetNum(GetActiveShardNum());
}

int32 UAsceEtherNetworkSubsystem::GetActiveShardNum() const
{
	return FMath::Max(8, FPlatformMisc::NumberOfCoresIncludingHyperthreads() + 8);
}

void UAsceEtherNetworkSubsystem::Deinitialize()
{
	ClearGraph();
	GraphCommands.Reset();
	Nodes.Reset();
	FreeIds.Reset();
	Ports.Reset();
	PortSegId.Reset();
	FreePortIds.Reset();
	EntityToNode.Reset();
	Super::Deinitialize();
}

bool UAsceEtherNetworkSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// 客户端不参与逻辑（表现后续从复制数据来）：不创建实例，避免空跑/误用
	if (World->IsNetMode(NM_Client))
	{
		return false;
	}
	return World->IsGameWorld() || World->IsPlayInEditor();
}

TStatId UAsceEtherNetworkSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAsceEtherNetworkSubsystem, STATGROUP_Tickables);
}

UAsceEtherNetworkSubsystem* UAsceEtherNetworkSubsystem::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<UAsceEtherNetworkSubsystem>() : nullptr;
}

void UAsceEtherNetworkSubsystem::Tick(float DeltaTime)
{
	// 自 tick 已停用：本 tick 体已挪成阶段函数，由 `UAsceSimulationSubsystem` 按固定顺序连续调用
	// （RunCommandDrainStage → RunSimulationStage），避免依赖 UE 的 tick 顺序。
	Super::Tick(DeltaTime);
}

// ── registry ──

int32 UAsceEtherNetworkSubsystem::AllocNodeId()
{
	int32 NodeId = INDEX_NONE;
	if (FreeIds.Num() > 0)
	{
		NodeId = FreeIds.Pop(EAllowShrinking::No);
	}
	else
	{
		NodeId = Nodes.AddDefaulted();
	}
	NodeCores.SetNum(Nodes.Num());
	// 器件邻接表也要跟 `Nodes` 对齐：只有全量重建会 SetNum 的话，纯增量路径（AddNode）会让它一直是空的
	// ⇒ Pass 1 读 DeviceAdjSegs[NodeId] 越界；且写侧（LinkDeviceSeg/RecomputeDeviceAdjSegsForNodes）带守卫会被静默跳过。
	DeviceAdjSegs.SetNum(Nodes.Num());
	return NodeId;
}

// ── 命令队列 ──

void UAsceEtherNetworkSubsystem::PushCommand(FInstancedStruct Command)
{
	if (Command.IsValid())
	{
		GraphCommands.Add(MoveTemp(Command));
	}
}

void UAsceEtherNetworkSubsystem::PushAddNode(FMassEntityHandle Entity, FName BuildingId)
{
	FAsceEtherCmd_AddNode Cmd;
	Cmd.EntityNumber = static_cast<int64>(Entity.AsNumber());
	Cmd.BuildingId = BuildingId;
	PushCommand(FInstancedStruct::Make<FAsceEtherCmd_AddNode>(Cmd));
}

void UAsceEtherNetworkSubsystem::PushRemoveNode(FMassEntityHandle Entity)
{
	FAsceEtherCmd_RemoveNode Cmd;
	Cmd.EntityNumber = static_cast<int64>(Entity.AsNumber());
	PushCommand(FInstancedStruct::Make<FAsceEtherCmd_RemoveNode>(Cmd));
}

void UAsceEtherNetworkSubsystem::PushPatchNode(FMassEntityHandle Entity, const FAsceEtherNodePatch& Patch)
{
	FAsceEtherCmd_PatchNode Cmd;
	Cmd.EntityNumber = static_cast<int64>(Entity.AsNumber());
	Cmd.Patch = Patch;
	PushCommand(FInstancedStruct::Make<FAsceEtherCmd_PatchNode>(Cmd));
}

void UAsceEtherNetworkSubsystem::PushSetNodeLinks(FMassEntityHandle Entity, const TArray<FAsceEtherLinkDef>& Links)
{
	FAsceEtherCmd_SetNodeLinks Cmd;
	Cmd.EntityNumber = static_cast<int64>(Entity.AsNumber());
	Cmd.Links = Links;
	PushCommand(FInstancedStruct::Make<FAsceEtherCmd_SetNodeLinks>(Cmd));
}

void UAsceEtherNetworkSubsystem::PushPatchPort(FMassEntityHandle Entity, const FAsceEtherSlotKey& Slot, const FAsceEtherPortPatch& Patch)
{
	FAsceEtherCmd_PatchPort Cmd;
	Cmd.EntityNumber = static_cast<int64>(Entity.AsNumber());
	Cmd.Slot = Slot;
	Cmd.Patch = Patch;
	PushCommand(FInstancedStruct::Make<FAsceEtherCmd_PatchPort>(Cmd));
}

void UAsceEtherNetworkSubsystem::PushRefreshData(const UScriptStruct* RowStruct)
{
	FAsceEtherCmd_RefreshData Cmd;
	Cmd.RowStruct = const_cast<UScriptStruct*>(RowStruct);
	PushCommand(FInstancedStruct::Make<FAsceEtherCmd_RefreshData>(Cmd));
}

void UAsceEtherNetworkSubsystem::PushFullRebuild()
{
	PushCommand(FInstancedStruct::Make<FAsceEtherCmd_FullRebuild>());
}

namespace
{
	/** 端口相对几何身份 → 64 位 key（(nodeId, Direction, Offset, Dimension) 打包，无 hash 碰撞）。 */
	FORCEINLINE uint64 MakeSlotKey(int32 NodeId, uint8 Direction, uint8 Offset, uint8 Dimension)
	{
		return (static_cast<uint64>(static_cast<uint32>(NodeId)) << 24)
			| (static_cast<uint64>(Direction) << 16)
			| (static_cast<uint64>(Offset) << 8)
			| static_cast<uint64>(Dimension);
	}
}

bool UAsceEtherNetworkSubsystem::IsCombinationalPort(int32 PortId) const
{
	if (!Ports.IsValidIndex(PortId) || !Ports[PortId].bActive)
	{
		return false;
	}
	const int32 NodeId = Ports[PortId].NodeId;
	return IsNodeAlive(NodeId) && !Nodes[NodeId].bDevice;
}

void UAsceEtherNetworkSubsystem::RefreshInitialActiveSegments()
{
	ActiveList.Reset();
	for (int32 SegId = 0; SegId < Segments.Num(); ++SegId)
	{
		if (!Segments[SegId].bActive)
		{
			continue;
		}
		bool bHasEther = false;
		for (const int32 PortId : Segments[SegId].Order)
		{
			if (Ports[PortId].Ether > 0)
			{
				bHasEther = true;
				break;
			}
		}
		if (bHasEther)
		{
			Segments[SegId].Stamp = CurrentStamp;
			ActiveList.Add(SegId);
		}
	}
}

void UAsceEtherNetworkSubsystem::RecomputeSegmentsForPorts(const TArray<int32>& TouchedPorts, TArray<int32>* OutAllAffectedPorts, const TArray<int32>* ExtraSegIds)
{
	// ① 受影响的旧段
	TArray<int32> OldSegs;
	for (const int32 PortId : TouchedPorts)
	{
		if (PortSegId.IsValidIndex(PortId) && PortSegId[PortId] != INDEX_NONE)
		{
			OldSegs.AddUnique(PortSegId[PortId]);
		}
	}
	if (ExtraSegIds)
	{
		for (const int32 SegId : *ExtraSegIds)
		{
			if (Segments.IsValidIndex(SegId) && Segments[SegId].bActive)
			{
				OldSegs.AddUnique(SegId);
			}
		}
	}

	// ② 重算范围 = 旧段全部端口 ∪ 受影响端口（只保留组合端口）
	TArray<int32> Comp;
	Comp.Reserve(32);
	for (const int32 SegId : OldSegs)
	{
		if (Segments.IsValidIndex(SegId) && Segments[SegId].bActive)
		{
			for (const int32 PortId : Segments[SegId].Order)
			{
				Comp.AddUnique(PortId);
			}
		}
	}
	for (const int32 PortId : TouchedPorts)
	{
		Comp.AddUnique(PortId);
	}
	Comp.RemoveAll([this](int32 PortId) { return !IsCombinationalPort(PortId); });

	if (OutAllAffectedPorts)
	{
		*OutAllAffectedPorts = Comp;
	}

	// ③ 清掉旧映射并回收旧段
	for (const int32 PortId : Comp)
	{
		PortSegId[PortId] = INDEX_NONE;
	}
	for (const int32 SegId : OldSegs)
	{
		FreeSegId(SegId);
	}
	if (Comp.Num() == 0)
	{
		return;
	}

	// ④ 范围内连通分量（后继 + 前驱）⇒ 段；段内 Kahn 拓扑序
	TSet<int32> InComp;
	InComp.Reserve(Comp.Num());
	for (const int32 PortId : Comp)
	{
		InComp.Add(PortId);
	}

	TArray<uint8> Visited;
	Visited.Init(0, Ports.Num());

	auto DecInDeg = [](TMap<int32, int32>& InDeg, int32 V)
	{
		if (int32* D = InDeg.Find(V))
		{
			if (--(*D) == 0)
			{
				return true;
			}
		}
		return false;
	};

	for (const int32 Seed : Comp)
	{
		if (Visited[Seed])
		{
			continue;
		}

		TArray<int32> Members;
		Members.Add(Seed);
		Visited[Seed] = 1;
		for (int32 Head = 0; Head < Members.Num(); ++Head)
		{
			const int32 U = Members[Head];
			auto VisitNeighbor = [&](int32 V)
			{
				if (InComp.Contains(V) && !Visited[V])
				{
					Visited[V] = 1;
					Members.Add(V);
				}
			};
			for (const int32 V : Ports[U].LinkTargets)
			{
				VisitNeighbor(V);
			}
			if (Ports[U].OutTargetPort != INDEX_NONE)
			{
				VisitNeighbor(Ports[U].OutTargetPort);
			}
			if (Ports[U].InSourcePort != INDEX_NONE)
			{
				VisitNeighbor(Ports[U].InSourcePort);
			}
		}

		TMap<int32, int32> InDeg;
		InDeg.Reserve(Members.Num());
		for (const int32 U : Members)
		{
			InDeg.Add(U, 0);
		}
		for (const int32 U : Members)
		{
			for (const int32 V : Ports[U].LinkTargets)
			{
				if (int32* D = InDeg.Find(V))
				{
					++(*D);
				}
			}
			if (int32* D = InDeg.Find(Ports[U].OutTargetPort))
			{
				++(*D);
			}
		}

		TArray<int32> Queue;
		for (const int32 U : Members)
		{
			if (InDeg[U] == 0)
			{
				Queue.Add(U);
			}
		}
		TArray<int32> Order;
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 U = Queue[Head];
			Order.Add(U);
			for (const int32 V : Ports[U].LinkTargets)
			{
				if (DecInDeg(InDeg, V))
				{
					Queue.Add(V);
				}
			}
			if (DecInDeg(InDeg, Ports[U].OutTargetPort))
			{
				Queue.Add(Ports[U].OutTargetPort);
			}
		}
		if (Order.Num() != Members.Num())
		{
			UE_LOG(LogTemp, Error, TEXT("[Ether] 组合环（局部重算）：范围内 %d 个端口只排出 %d 个（环上端口拒绝处理）"),
				Members.Num(), Order.Num());
		}

		const int32 SegId = AllocSegId();
		FAsceEtherSegment& Seg = Segments[SegId];
		Seg.Order = MoveTemp(Order);
		for (const int32 U : Seg.Order)
		{
			PortSegId[U] = SegId;
		}
	}
}

void UAsceEtherNetworkSubsystem::RecomputeDeviceAdjSegsForNodes(const TArray<int32>& DeviceNodesToUpdate)
{
	for (const int32 NodeId : DeviceNodesToUpdate)
	{
		if (!DeviceAdjSegs.IsValidIndex(NodeId))
		{
			continue;
		}
		TArray<int32>& Adj = DeviceAdjSegs[NodeId];
		Adj.Reset();

		const int32 PortCount = GetNodePortCount(NodeId);
		for (int32 i = 0; i < PortCount; ++i)
		{
			const int32 PortId = GetNodePortId(NodeId, i);
			if (!Ports.IsValidIndex(PortId) || !Ports[PortId].bActive)
			{
				continue;
			}
			auto AddSegOf = [&](int32 OtherPortId)
			{
				if (IsCombinationalPort(OtherPortId))
				{
					const int32 Seg = PortSegId[OtherPortId];
					if (Seg != INDEX_NONE)
					{
						Adj.AddUnique(Seg);
					}
				}
			};
			for (const int32 ToPort : Ports[PortId].LinkTargets)
			{
				AddSegOf(ToPort);
			}
			if (Ports[PortId].OutTargetPort != INDEX_NONE)
			{
				AddSegOf(Ports[PortId].OutTargetPort);
			}
			if (Ports[PortId].InSourcePort != INDEX_NONE)
			{
				AddSegOf(Ports[PortId].InSourcePort);
			}
		}
	}
}

void UAsceEtherNetworkSubsystem::RebuildNodeInternalTopology(int32 NodeId, TArray<int32>& OutTouchedPorts)
{
	OutTouchedPorts.Reset();
	if (!IsNodeAlive(NodeId))
	{
		return;
	}
	FNode& N = Nodes[NodeId];
	const int32 PortCount = N.SlotPortIds.Num();

	// 1) 清掉旧内布线：本建筑端口的 LinkTargets + 从目标端口的 InSources 里移除自己
	for (int32 i = 0; i < PortCount; ++i)
	{
		const int32 PortId = N.SlotPortIds[i];
		if (!Ports.IsValidIndex(PortId) || !Ports[PortId].bActive)
		{
			continue;
		}
		FAsceEtherPortData& P = Ports[PortId];
		for (const int32 ToPort : P.LinkTargets)
		{
			if (Ports.IsValidIndex(ToPort))
			{
				Ports[ToPort].InSources.RemoveSingle(PortId);
			}
		}
		P.LinkTargets.Reset();
		OutTouchedPorts.AddUnique(PortId);
	}

	// 2) 按 InternalLinks 重建（校验：不自连 / 不越界）
	for (const FAsceEtherLinkDef& LD : N.InternalLinks)
	{
		if (LD.FromPort == LD.ToPort
			|| !N.SlotPortIds.IsValidIndex(LD.FromPort)
			|| !N.SlotPortIds.IsValidIndex(LD.ToPort))
		{
			continue;
		}
		const int32 FromPort = N.SlotPortIds[LD.FromPort];
		const int32 ToPort = N.SlotPortIds[LD.ToPort];
		if (!Ports.IsValidIndex(FromPort) || !Ports.IsValidIndex(ToPort)
			|| !Ports[FromPort].bActive || !Ports[ToPort].bActive)
		{
			continue;
		}
		Ports[FromPort].LinkTargets.AddUnique(ToPort);
		Ports[ToPort].InSources.AddUnique(FromPort);
		OutTouchedPorts.AddUnique(ToPort);
	}

	// 3) 扫描序排序（相对源端口左手边起顺时针）
	for (int32 i = 0; i < PortCount; ++i)
	{
		const int32 PortId = N.SlotPortIds[i];
		if (!Ports.IsValidIndex(PortId) || !Ports[PortId].bActive)
		{
			continue;
		}
		FAsceEtherPortData& P = Ports[PortId];
		if (P.LinkTargets.Num() <= 1)
		{
			continue;
		}
		const int32 SrcDir = static_cast<int32>(P.Direction);
		P.LinkTargets.Sort([this, SrcDir](int32 A, int32 B)
		{
			const FAsceEtherPortData& PA = Ports[A];
			const FAsceEtherPortData& PB = Ports[B];
			const int32 RA = (static_cast<int32>(PA.Direction) - SrcDir + 1) & 3;
			const int32 RB = (static_cast<int32>(PB.Direction) - SrcDir + 1) & 3;
			if (RA != RB)
			{
				return RA < RB;
			}
			if (PA.Dimension != PB.Dimension)
			{
				return PA.Dimension < PB.Dimension;
			}
			if (PA.CellY != PB.CellY)
			{
				return PA.CellY < PB.CellY;
			}
			if (PA.CellX != PB.CellX)
			{
				return PA.CellX < PB.CellX;
			}
			return A < B;
		});
	}

	// 4) 入边列表规范为升序（pull 顺序确定；单数组，无平行数组对齐问题）
	for (const int32 PortId : OutTouchedPorts)
	{
		if (!Ports.IsValidIndex(PortId) || !Ports[PortId].bActive)
		{
			continue;
		}
		FAsceEtherPortData& P = Ports[PortId];
		if (P.InSources.Num() > 1)
		{
			P.InSources.Sort();
		}
	}
}

void UAsceEtherNetworkSubsystem::DetachNodeFromGraph(int32 NodeId)
{
	if (!IsNodeAlive(NodeId))
	{
		return;
	}
	for (const int32 PortId : Nodes[NodeId].SlotPortIds)
	{
		if (!Ports.IsValidIndex(PortId) || !Ports[PortId].bActive)
		{
			continue;
		}
		FAsceEtherPortData& P = Ports[PortId];

		// ① 入边（内部源/外部源）：把"我"从对方的出边里摘掉
		for (const int32 SrcPort : P.InSources)
		{
			if (!Ports.IsValidIndex(SrcPort))
			{
				continue;
			}
			Ports[SrcPort].LinkTargets.RemoveSingle(PortId);
			if (Ports[SrcPort].OutTargetPort == PortId)
			{
				Ports[SrcPort].OutTargetPort = INDEX_NONE;
			}
		}

		// ② 出边：外部出边（顺带清对端的 InSourcePort/InSources）；内部布线目标同理
		if (P.OutTargetPort != INDEX_NONE)
		{
			ClearExternalEdge(PortId);
		}
		for (const int32 ToPort : P.LinkTargets)
		{
			if (Ports.IsValidIndex(ToPort))
			{
				Ports[ToPort].InSources.RemoveSingle(PortId);
			}
		}

		// ③ 自身图字段清零（在制品/玩家配置保留）
		P.LinkTargets.Reset();
		P.InSources.Reset();
		P.OutTargetPort = INDEX_NONE;
		P.InSourcePort = INDEX_NONE;
	}
}

void UAsceEtherNetworkSubsystem::BuildNodePorts(int32 NodeId, const FAsceTickContext& Ctx)
{
	if (!IsNodeAlive(NodeId))
	{
		return;
	}
	// 先脱网：无论后面是否因缺配置/缺 footprint 提前返回，都不会留下幽灵引用
	DetachNodeFromGraph(NodeId);

	FNode& N = Nodes[NodeId];
	const FAsceEtherBuildingDef* Cfg = GetNodeDef(NodeId);
	if (!Cfg)
	{
		// def 缺失（AddNode/PatchNode 解析失败时已 ensure 报错）⇒ 该节点按"空壳"处理：没有端口/core
		return;
	}
	N.bDevice = (Cfg->DeviceType == EAsceEtherDeviceType::Sequential);

	// 摆放来自建筑系统的 footprint（Ether 只读）
	const FMassEntityManager* EM = Ctx.EntityManager;
	const FAsceGridFootprintFragment* Foot = EM ? EM->GetFragmentDataPtr<FAsceGridFootprintFragment>(N.Entity) : nullptr;
	if (!Foot)
	{
		return;
	}

	// 建筑朝向：从实体 Transform 读（建筑系统写入 = Rotation × 90°）；端口的世界方向/格位都按它旋转
	N.CachedDirection = GetNodeRotation(N.Entity, Ctx);

	// footprint 缓存：搬家/拆除后要靠"旧位置"重扫邻居（实体销毁后 fragment 就取不到了）
	N.CachedFootprint = *Foot;
	N.bHasCachedFootprint = true;

	// **端口持久化 + 身份复用**：按"相对几何身份"（Direction/Offset/Dimension）匹配旧端口 ⇒ id 不变、运行态原地保留
	const TArray<int32> OldPortIds = N.SlotPortIds;   // 旧映射（副本；匹配用）
	TArray<uint8> OldUsed;
	OldUsed.Init(0, OldPortIds.Num());

	TArray<int32> NewPortIds;
	NewPortIds.Reserve(Cfg->PortSlots.Num());
	for (int32 i = 0; i < Cfg->PortSlots.Num(); ++i)
	{
		const FAsceEtherPortSlot& Slot = Cfg->PortSlots[i];

		// 1) 找几何身份相同的旧端口（移动/旋转不改身份；改建筑配置 = 换接口 ⇒ 匹配不到就新建）
		int32 PortId = INDEX_NONE;
		for (int32 o = 0; o < OldPortIds.Num(); ++o)
		{
			if (OldUsed[o])
			{
				continue;
			}
				const int32 OldId = OldPortIds[o];
				// 命中条件：槽位在用 **且仍属于本建筑** 且几何身份相同
				if (Ports.IsValidIndex(OldId) && Ports[OldId].bActive
					&& Ports[OldId].NodeId == NodeId
					&& Ports[OldId].SlotDirection == Slot.Direction
					&& Ports[OldId].Offset == Slot.Offset
					&& Ports[OldId].Dimension == Slot.Dimension)
			{
				PortId = OldId;
				OldUsed[o] = 1;
				break;
			}
		}

		if (PortId == INDEX_NONE)
		{
			// 2) 新端口：取 slot 的默认值
			PortId = AllocPortId();
			FAsceEtherPortData& NewPort = Ports[PortId];
			NewPort.PortTypeId = Slot.PortType;
			NewPort.SplitMode = Slot.SplitMode;
			NewPort.Dimension = Slot.Dimension;
			NewPort.SlotDirection = Slot.Direction;
			NewPort.Direction = static_cast<uint8>((Slot.Direction + N.CachedDirection) & 3);
			NewPort.Offset = Slot.Offset;
			NewPort.TransportType = EAsceEtherTransportType::Any;
		}
		FAsceEtherPortData& P = Ports[PortId];

		// 3) 位置/朝向始终按当前 slot（世界方向 = 本地方向 + 建筑朝向）；图相关字段清空，实例字段/在制品保留
		P.NodeId = NodeId;
		P.bActive = true;
		P.SlotDirection = Slot.Direction;
		P.Direction = static_cast<uint8>((Slot.Direction + N.CachedDirection) & 3);
		P.Offset = Slot.Offset;
		P.Dimension = Slot.Dimension;
		P.LinkTargets.Reset();
		P.OutTargetPort = INDEX_NONE;
		P.InSourcePort = INDEX_NONE;
		P.InSources.Reset();

		// 4) 端口类型：查端口类型表，解析 Op / Throughput / LinkRange
		ResolvePortTypeConfig(P);

		// 5) 摆放（按槽位本地方向 + 建筑朝向，落到最终 AABB 里）
		const FIntPoint Cell = SlotToCell(Foot->OriginGridLocation, Foot->SizeX, Foot->SizeY, P.SlotDirection, P.Offset, N.CachedDirection);
		P.CellX = Cell.X;
		P.CellY = Cell.Y;

		if (P.Dimension >= Cfg->SupportedDimCount)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Ether] 端口维度超出 SupportedDimCount：%s slot %d（Dim=%d, Supported=%d）"),
				*N.BuildingId.ToString(), i, P.Dimension, Cfg->SupportedDimCount);
		}

		NewPortIds.Add(PortId);
	}

	// 6) 回收不再使用的旧端口（在制品随之丢弃）：回收前先摘掉自己的出边，保证不留悬空引用
	for (int32 o = 0; o < OldPortIds.Num(); ++o)
	{
		if (!OldUsed[o])
		{
			const int32 OldId = OldPortIds[o];
			if (Ports.IsValidIndex(OldId) && Ports[OldId].bActive && Ports[OldId].OutTargetPort != INDEX_NONE)
			{
				ClearExternalEdge(OldId);
			}
			FreePortId(OldId);
		}
	}
	N.SlotPortIds = MoveTemp(NewPortIds);

	// 内部布线：只在"首次初始化"时用配置默认值；玩家清空后保持空（否则清空会被重建回填）
	if (!N.bLinksInitialized)
	{
		N.InternalLinks = Cfg->DefaultInternalLinks;
		N.bLinksInitialized = true;
	}
}

void UAsceEtherNetworkSubsystem::EnsureNodeCore(int32 NodeId)
{
	if (!IsNodeAlive(NodeId))
	{
		return;
	}
	const FAsceEtherBuildingDef* Cfg = GetNodeDef(NodeId);
	UClass* CoreClass = Cfg ? Cfg->ProcessCoreClass.LoadSynchronous() : nullptr;
	if (!CoreClass)
	{
		NodeCores[NodeId] = nullptr;
		return;
	}
	UAsceEtherProcessCore* CDO = CoreClass->GetDefaultObject<UAsceEtherProcessCore>();
	if (!NodeCores[NodeId] || NodeCores[NodeId]->GetClass() != CoreClass)
	{
		NodeCores[NodeId] = (CDO && CDO->bInstance) ? NewObject<UAsceEtherProcessCore>(this, CoreClass) : CDO;
	}
	if (UAsceEtherProcessCore* Core = NodeCores[NodeId].Get())
	{
		Core->OnInit(*this, NodeId);
	}
}

int32 UAsceEtherNetworkSubsystem::RayScanPort(int32 PortId, const UAsceSpatialGridSubsystem* Grid) const
{
	if (!Ports.IsValidIndex(PortId) || !Ports[PortId].bActive)
	{
		return INDEX_NONE;
	}
	const FAsceEtherPortData& SPort = Ports[PortId];
	if (!IsEmittingPort(SPort) || SPort.LinkRange <= 0)
	{
		return INDEX_NONE;
	}

	const uint8 OppDir = static_cast<uint8>(AsceEther::Opposite(static_cast<AsceEther::EDir>(SPort.Direction)));
	if (!Grid)
	{
		return INDEX_NONE;
	}

	FMassEntityHandle HitBuilding;
	FIntPoint EnterCell = FIntPoint::ZeroValue;
	if (!Grid->FindBuildingOnRay(FIntPoint(SPort.CellX, SPort.CellY), SPort.Direction, SPort.LinkRange, HitBuilding, EnterCell))
	{
		return INDEX_NONE;
	}
	const int32 HitNode = FindNodeByEntity(HitBuilding);
	if (HitNode == INDEX_NONE || HitNode == SPort.NodeId)
	{
		return INDEX_NONE;
	}

	const int32 BCount = GetNodePortCount(HitNode);
	for (int32 qi = 0; qi < BCount; ++qi)
	{
		const int32 QPortId = GetNodePortId(HitNode, qi);
		if (!Ports.IsValidIndex(QPortId))
		{
			continue;
		}
		const FAsceEtherPortData& Q = Ports[QPortId];
		if (Q.bActive && IsAcceptingPort(Q) && Q.Direction == OppDir && Q.Dimension == SPort.Dimension
			&& Q.CellX == EnterCell.X && Q.CellY == EnterCell.Y)
		{
			return QPortId;
		}
	}

	return INDEX_NONE;
}

void UAsceEtherNetworkSubsystem::SetExternalEdge(int32 SrcPort, int32 DstPort)
{
	if (!Ports.IsValidIndex(SrcPort) || !Ports.IsValidIndex(DstPort))
	{
		return;
	}
	ClearExternalEdge(SrcPort);
	Ports[SrcPort].OutTargetPort = DstPort;
	Ports[DstPort].InSourcePort = SrcPort;
	if (!Ports[DstPort].InSources.Contains(SrcPort))
	{
		Ports[DstPort].InSources.Add(SrcPort);
		Ports[DstPort].InSources.Sort();   // pull 顺序确定（按 PortId 升序）
	}
}

void UAsceEtherNetworkSubsystem::ClearExternalEdge(int32 SrcPort)
{
	if (!Ports.IsValidIndex(SrcPort))
	{
		return;
	}
	const int32 DstPort = Ports[SrcPort].OutTargetPort;
	if (DstPort == INDEX_NONE)
	{
		return;
	}
	Ports[SrcPort].OutTargetPort = INDEX_NONE;
	if (Ports.IsValidIndex(DstPort))
	{
		if (Ports[DstPort].InSourcePort == SrcPort)
		{
			Ports[DstPort].InSourcePort = INDEX_NONE;
		}
		Ports[DstPort].InSources.RemoveSingle(SrcPort);   // RemoveSingle 保序（升序不变式不破）
	}
}

void UAsceEtherNetworkSubsystem::CollectAdjacentSourcePorts(int32 NodeId, const FAsceGridFootprintFragment& Foot, const UAsceSpatialGridSubsystem* Grid, TArray<int32>& OutSources) const
{
	if (!Grid)
	{
		return;
	}
	const int32 ScanLimit = FMath::Max(1, MaxPortLinkRange);

	for (int32 y = 0; y < Foot.SizeY; ++y)
	{
		for (int32 x = 0; x < Foot.SizeX; ++x)
		{
			const FIntPoint BaseCell(Foot.OriginGridLocation.X + x, Foot.OriginGridLocation.Y + y);
			for (uint8 d = 0; d < 4; ++d)
			{
				const uint8 BackDir = static_cast<uint8>(AsceEther::Opposite(static_cast<AsceEther::EDir>(d)));
				FMassEntityHandle HitBuilding;
				FIntPoint EnterCell = FIntPoint::ZeroValue;
				if (!Grid->FindBuildingOnRay(BaseCell, BackDir, ScanLimit, HitBuilding, EnterCell))
				{
					continue;
				}
				const int32 B = FindNodeByEntity(HitBuilding);
				if (B == INDEX_NONE || B == NodeId)
				{
					continue;
				}
				const int32 BCount = GetNodePortCount(B);
				for (int32 qi = 0; qi < BCount; ++qi)
				{
					const int32 QPortId = GetNodePortId(B, qi);
					if (Ports.IsValidIndex(QPortId) && Ports[QPortId].bActive
						&& Ports[QPortId].Direction == d && IsEmittingPort(Ports[QPortId]))
					{
						OutSources.AddUnique(QPortId);
					}
				}
			}
		}
	}
}

void UAsceEtherNetworkSubsystem::RescanExternalEdgesForNode(int32 NodeId, TArray<int32>& OutTouchedPorts, const FAsceTickContext& Ctx, const FAsceGridFootprintFragment* ExtraFootprint)
{
	OutTouchedPorts.Reset();
	if (!IsNodeAlive(NodeId))
	{
		return;
	}
	FNode& N = Nodes[NodeId];

	// ① 候选来源端口：本建筑自己的"发"端口 + 现有入边的来源 + "会被本建筑挡住/放开"的那些来源
	TArray<int32> Sources;
	const int32 PortCount = N.SlotPortIds.Num();
	for (int32 i = 0; i < PortCount; ++i)
	{
		const int32 PortId = N.SlotPortIds[i];
		if (!Ports.IsValidIndex(PortId) || !Ports[PortId].bActive)
		{
			continue;
		}
		if (IsEmittingPort(Ports[PortId]))
		{
			Sources.AddUnique(PortId);
		}
		if (Ports[PortId].InSourcePort != INDEX_NONE)
		{
			Sources.AddUnique(Ports[PortId].InSourcePort);
		}
	}

	const FMassEntityManager* EM = Ctx.EntityManager;
	const FAsceGridFootprintFragment* Foot = EM ? EM->GetFragmentDataPtr<FAsceGridFootprintFragment>(N.Entity) : nullptr;

	// ①b 当前位置的邻居
	if (Foot)
	{
		CollectAdjacentSourcePorts(NodeId, *Foot, Ctx.Grid, Sources);
	}

	// ①c **旧位置**的邻居（搬家/拆除时：旧位置挡住或放开过的那些来源必须一起重扫，
	//     否则"搬走后本该连通的那条线"不会补上、"旧位置挡住的线"也不会撤销）
	if (N.bHasCachedFootprint)
	{
		const bool bSameAsCurrent = Foot
			&& Foot->OriginGridLocation == N.CachedFootprint.OriginGridLocation
			&& Foot->SizeX == N.CachedFootprint.SizeX
			&& Foot->SizeY == N.CachedFootprint.SizeY;
		const bool bSameAsExtra = ExtraFootprint
			&& ExtraFootprint->OriginGridLocation == N.CachedFootprint.OriginGridLocation
			&& ExtraFootprint->SizeX == N.CachedFootprint.SizeX
			&& ExtraFootprint->SizeY == N.CachedFootprint.SizeY;
		if (!bSameAsCurrent && !bSameAsExtra)
		{
			CollectAdjacentSourcePorts(NodeId, N.CachedFootprint, Ctx.Grid, Sources);
		}
	}

	// ①d 调用方显式给的"旧 footprint"（`PatchNode(摆放)` 场景：`BuildNodePorts` 已经把缓存刷成新位置了，
	//     所以旧位置必须由调用方在重建端口**之前**抓下来传进来）
	if (ExtraFootprint)
	{
		const bool bSameAsCurrent = Foot
			&& Foot->OriginGridLocation == ExtraFootprint->OriginGridLocation
			&& Foot->SizeX == ExtraFootprint->SizeX
			&& Foot->SizeY == ExtraFootprint->SizeY;
		if (!bSameAsCurrent)
		{
			CollectAdjacentSourcePorts(NodeId, *ExtraFootprint, Ctx.Grid, Sources);
		}
	}

	// ② 清掉这些来源端口的旧出边 + 本建筑端口的入边（走工具函数，同步维护 InSources/记录数组）
	for (const int32 Src : Sources)
	{
		if (!Ports.IsValidIndex(Src) || !Ports[Src].bActive)
		{
			continue;
		}
		if (Ports[Src].OutTargetPort != INDEX_NONE)
		{
			OutTouchedPorts.AddUnique(Ports[Src].OutTargetPort);
			OutTouchedPorts.AddUnique(Src);
			ClearExternalEdge(Src);
		}
	}
	for (int32 i = 0; i < PortCount; ++i)
	{
		const int32 PortId = N.SlotPortIds[i];
		if (!Ports.IsValidIndex(PortId) || !Ports[PortId].bActive)
		{
			continue;
		}
		const int32 OldSource = Ports[PortId].InSourcePort;
		if (OldSource != INDEX_NONE)
		{
			OutTouchedPorts.AddUnique(OldSource);
			OutTouchedPorts.AddUnique(PortId);
			ClearExternalEdge(OldSource);
		}
	}

	// ③ 重扫
	for (const int32 Src : Sources)
	{
		if (!Ports.IsValidIndex(Src) || !Ports[Src].bActive)
		{
			continue;
		}
		const int32 NewTarget = RayScanPort(Src, Ctx.Grid);
		if (NewTarget != INDEX_NONE)
		{
			SetExternalEdge(Src, NewTarget);
			OutTouchedPorts.AddUnique(Src);
			OutTouchedPorts.AddUnique(NewTarget);
		}
	}
}

void UAsceEtherNetworkSubsystem::BuildGraphDigest(TArray<FString>& OutDigest) const
{
	OutDigest.Reset();

	// 端口：出/入目标 + 内部布线目标 + 入边源（后两者排序后比较，避免顺序差异）
	for (int32 PortId = 0; PortId < Ports.Num(); ++PortId)
	{
		if (!Ports.IsValidIndex(PortId) || !Ports[PortId].bActive)
		{
			continue;
		}
		const FAsceEtherPortData& P = Ports[PortId];
		FAsceEtherPortIdList Targets = P.LinkTargets;
		Targets.Sort();
		FAsceEtherPortIdList InSrc = P.InSources;
		InSrc.Sort();
		OutDigest.Add(FString::Printf(TEXT("P%d Out=%d In=%d Tgt=%s Src=%s"),
			PortId, P.OutTargetPort, P.InSourcePort,
			*FString::JoinBy(Targets, TEXT(","), [](int32 V) { return FString::FromInt(V); }),
			*FString::JoinBy(InSrc, TEXT(","), [](int32 V) { return FString::FromInt(V); })));
	}

	// 段：用"端口集合"当签名（两条路径的段 id 可能不同，不能直接比 id）
	TArray<FString> SegSigs;
	for (int32 SegId = 0; SegId < Segments.Num(); ++SegId)
	{
		if (!Segments[SegId].bActive)
		{
			continue;
		}
		TArray<int32> Order = Segments[SegId].Order;
		Order.Sort();
		SegSigs.Add(FString::JoinBy(Order, TEXT(","), [](int32 V) { return FString::FromInt(V); }));
	}
	SegSigs.Sort();
	for (const FString& Sig : SegSigs)
	{
		OutDigest.Add(FString::Printf(TEXT("Seg %s"), *Sig));
	}

	// 器件相邻段：同样用"相邻段里所有端口"的集合当签名
	for (int32 NodeId = 0; NodeId < Nodes.Num(); ++NodeId)
	{
		if (!IsNodeAlive(NodeId) || !Nodes[NodeId].bDevice)
		{
			continue;
		}
		TArray<int32> AdjPorts;
		if (DeviceAdjSegs.IsValidIndex(NodeId))
		{
			for (const int32 SegId : DeviceAdjSegs[NodeId])
			{
				if (Segments.IsValidIndex(SegId) && Segments[SegId].bActive)
				{
					for (const int32 PortId : Segments[SegId].Order)
					{
						AdjPorts.AddUnique(PortId);
					}
				}
			}
		}
		AdjPorts.Sort();
		OutDigest.Add(FString::Printf(TEXT("N%d Adj=%s"), NodeId,
			*FString::JoinBy(AdjPorts, TEXT(","), [](int32 V) { return FString::FromInt(V); })));
	}

	// 节点：footprint 缓存（局部 vs 全量的差异也会体现在这里；搬家/拆除的重扫依赖它）
	for (int32 NodeId = 0; NodeId < Nodes.Num(); ++NodeId)
	{
		const FNode& N = Nodes[NodeId];
		if (!N.bAlive)
		{
			continue;
		}
		OutDigest.Add(FString::Printf(TEXT("N%d Foot=%s Origin=%d,%d Size=%d,%d Rot=%d"),
			NodeId,
			N.bHasCachedFootprint ? TEXT("1") : TEXT("0"),
			N.CachedFootprint.OriginGridLocation.X, N.CachedFootprint.OriginGridLocation.Y,
			N.CachedFootprint.SizeX, N.CachedFootprint.SizeY,
			static_cast<int32>(N.CachedDirection)));
	}

	OutDigest.Sort();
}

void UAsceEtherNetworkSubsystem::ValidateIncrementalAgainstFullRebuild(const FAsceTickContext& Ctx)
{
	TArray<FString> LocalDigest;
	BuildGraphDigest(LocalDigest);

	// 全量重建（端口持久化 ⇒ 结果应当与局部更新一致；不一致时以全量结果为准）
	RebuildGraphInternal(Ctx);

	TArray<FString> FullDigest;
	BuildGraphDigest(FullDigest);

	if (LocalDigest == FullDigest)
	{
		UE_LOG(LogTemp, Log, TEXT("[Ether] 增量校验通过（%d 条摘要）"), LocalDigest.Num());
		return;
	}

	UE_LOG(LogTemp, Error, TEXT("[Ether] 增量校验**不一致**：局部 %d 条 vs 全量 %d 条（已用全量结果兜底）"),
		LocalDigest.Num(), FullDigest.Num());
	TSet<FString> LocalSet(LocalDigest);
	TSet<FString> FullSet(FullDigest);
	int32 Logged = 0;
	for (const FString& Line : LocalDigest)
	{
		if (!FullSet.Contains(Line) && Logged++ < 10)
		{
			UE_LOG(LogTemp, Error, TEXT("[Ether]   仅局部有：%s"), *Line);
		}
	}
	for (const FString& Line : FullDigest)
	{
		if (!LocalSet.Contains(Line) && Logged++ < 20)
		{
			UE_LOG(LogTemp, Error, TEXT("[Ether]   仅全量有：%s"), *Line);
		}
	}
}

void UAsceEtherNetworkSubsystem::ResolvePortTypeConfig(FAsceEtherPortData& P)
{
	FAsceEtherPortTypeRow TypeRow;
	if (UAsceDataTableSubsystem::GetDefinition<FAsceEtherPortTypeRow>(P.PortTypeId, TypeRow))
	{
		P.Op = TypeRow.Op;
		P.Throughput = static_cast<uint16>(FMath::Clamp<int32>(TypeRow.ThroughputPerTick, 1, 65535));
		P.LinkRange = TypeRow.LinkRangeCells;
	}
	else
	{
		P.Op = EAsceEtherPortOp::Wire;
		P.Throughput = 1;
		P.LinkRange = 0;
		if (!P.PortTypeId.IsNone())
		{
			UE_LOG(LogTemp, Warning, TEXT("[Ether] 端口类型行不存在：node %d（PortTypeId=%s）⇒ 用默认值 Wire/1/0"),
				P.NodeId, *P.PortTypeId.ToString());
		}
	}
	MaxPortLinkRange = FMath::Max(MaxPortLinkRange, static_cast<int32>(P.LinkRange));
}

void UAsceEtherNetworkSubsystem::GatherAdjacentDeviceNodes(const TArray<int32>& TouchedPorts, TArray<int32>& OutDeviceNodes) const
{
	OutDeviceNodes.Reset();
	for (const int32 PortId : TouchedPorts)
	{
		if (!Ports.IsValidIndex(PortId) || !Ports[PortId].bActive)
		{
			continue;
		}
		auto AddIfDevice = [&](int32 OtherPortId)
		{
			if (Ports.IsValidIndex(OtherPortId) && Ports[OtherPortId].bActive)
			{
				const int32 OtherNode = Ports[OtherPortId].NodeId;
				if (IsNodeAlive(OtherNode) && Nodes[OtherNode].bDevice)
				{
					OutDeviceNodes.AddUnique(OtherNode);
				}
			}
		};
		AddIfDevice(PortId);
		for (const int32 V : Ports[PortId].LinkTargets)
		{
			AddIfDevice(V);
		}
		if (Ports[PortId].OutTargetPort != INDEX_NONE)
		{
			AddIfDevice(Ports[PortId].OutTargetPort);
		}
		if (Ports[PortId].InSourcePort != INDEX_NONE)
		{
			AddIfDevice(Ports[PortId].InSourcePort);
		}
	}
}

void UAsceEtherNetworkSubsystem::ApplyGraphCommands(const FAsceTickContext& Ctx)
{
	bool bNeedRebuild = false;
	bool bLocalChanged = false;

	for (const FInstancedStruct& Command : GraphCommands)
	{
		if (const FAsceEtherCmd_AddNode* Add = Command.GetPtr<FAsceEtherCmd_AddNode>())
		{
			const FMassEntityHandle Entity = FMassEntityHandle::FromNumber(static_cast<uint64>(Add->EntityNumber));
			if (!EntityToNode.Contains(Entity))
			{
				const int32 NodeId = AllocNodeId();
				FNode& N = Nodes[NodeId];
				N.Entity = Entity;
				N.BuildingId = Add->BuildingId;
				N.bAlive = true;
				N.DefData = FAsceEtherBuildingDef();
				N.bHasDefData = UAsceEntityDefLibrary::GetEtherBuildingDefByName(Add->BuildingId, N.DefData);
				// 契约：实体定义表必须在实体创建**之前**挂载完成（不支持"表挂晚了"的补救）。
				// 查不到就是数据/挂载顺序错误 ⇒ ensure 报错（击中断点 + 日志），但不做重试：
				// 该节点会作为"空壳"存在（没有端口/core，对 Ether 不可见）。
				ensureMsgf(N.bHasDefData,
					TEXT("[Ether] 建筑类型定义缺失：%s（行不存在 / 不是 Ether 建筑 / 表没挂载）——节点将没有端口"),
					*Add->BuildingId.ToString());
				N.InternalLinks.Reset();
				N.bHasCachedFootprint = false;   // 新节点：清掉可能残留的 footprint 缓存
				if (NodeCores.IsValidIndex(NodeId))
				{
					NodeCores[NodeId] = nullptr;
				}
				EntityToNode.Add(Entity, NodeId);
				// **局部**：端口 → core → 内布线 → 射线重扫 → 段/器件邻接
				BuildNodePorts(NodeId, Ctx);
				EnsureNodeCore(NodeId);

				TArray<int32> Touched;
				RebuildNodeInternalTopology(NodeId, Touched);
				TArray<int32> RescanTouched;
				RescanExternalEdgesForNode(NodeId, RescanTouched, Ctx);
				for (const int32 PortId : RescanTouched)
				{
					Touched.AddUnique(PortId);
				}

				TArray<int32> AllAffected;
				RecomputeSegmentsForPorts(Touched, &AllAffected);
				TArray<int32> DevNodes;
				GatherAdjacentDeviceNodes(AllAffected, DevNodes);
				RecomputeDeviceAdjSegsForNodes(DevNodes);
				if (Nodes[NodeId].bDevice)
				{
					// DeviceNodes 保持升序（Pass 1/2 只遍历它）：二分插入
					const int32 InsertAt = Algo::LowerBound(DeviceNodes, NodeId);
					if (!DeviceNodes.IsValidIndex(InsertAt) || DeviceNodes[InsertAt] != NodeId)
					{
						DeviceNodes.Insert(NodeId, InsertAt);
					}
				}
				bLocalChanged = true;
			}
		}
		else if (const FAsceEtherCmd_RemoveNode* Remove = Command.GetPtr<FAsceEtherCmd_RemoveNode>())
		{
			const FMassEntityHandle Entity = FMassEntityHandle::FromNumber(static_cast<uint64>(Remove->EntityNumber));
			if (const int32* Found = EntityToNode.Find(Entity))
			{
				const int32 NodeId = *Found;

				// **局部**：重扫受影响的边 → **先回收节点/端口** → 再重算段。
				// 段重算放在回收之后：死端口会被 `IsCombinationalPort` 过滤掉，不会残留在段 Order 里
				// （否则端口 id 被池子复用后，旧段会重复处理新端口）。
				TArray<int32> OldSegs;
				const int32 PortCount = GetNodePortCount(NodeId);
				TArray<int32> NodePorts;
				NodePorts.Reserve(PortCount);
				for (int32 i = 0; i < PortCount; ++i)
				{
					const int32 PortId = GetNodePortId(NodeId, i);
					NodePorts.Add(PortId);
					if (PortSegId.IsValidIndex(PortId) && PortSegId[PortId] != INDEX_NONE)
					{
						OldSegs.AddUnique(PortSegId[PortId]);
					}
				}

				TArray<int32> Touched = NodePorts;
				TArray<int32> RescanTouched;
				RescanExternalEdgesForNode(NodeId, RescanTouched, Ctx);
				for (const int32 PortId : RescanTouched)
				{
					Touched.AddUnique(PortId);
				}
				// 该建筑自己的出边在删除时不要重连：端口马上要被回收，避免下游残留指向死端口
				for (const int32 PortId : NodePorts)
				{
					if (Ports.IsValidIndex(PortId) && Ports[PortId].bActive && Ports[PortId].OutTargetPort != INDEX_NONE)
					{
						ClearExternalEdge(PortId);
					}
				}

				RemoveNodeInternal(NodeId);
				// DeviceNodes 保持升序：二分删除
				{
					const int32 RemoveAt = Algo::LowerBound(DeviceNodes, NodeId);
					if (DeviceNodes.IsValidIndex(RemoveAt) && DeviceNodes[RemoveAt] == NodeId)
					{
						DeviceNodes.RemoveAt(RemoveAt, 1, EAllowShrinking::No);
					}
				}

				TArray<int32> AllAffected;
				RecomputeSegmentsForPorts(Touched, &AllAffected, &OldSegs);

				TArray<int32> DevNodes;
				GatherAdjacentDeviceNodes(AllAffected, DevNodes);
				RecomputeDeviceAdjSegsForNodes(DevNodes);
				bLocalChanged = true;
			}
		}
		else if (const FAsceEtherCmd_PatchNode* PatchNode = Command.GetPtr<FAsceEtherCmd_PatchNode>())
		{
			const FMassEntityHandle Entity = FMassEntityHandle::FromNumber(static_cast<uint64>(PatchNode->EntityNumber));
			const int32* NodeId = EntityToNode.Find(Entity);
			if (NodeId && Nodes.IsValidIndex(*NodeId))
			{
				FNode& N = Nodes[*NodeId];
				if (PatchNode->Patch.Mask & FAsceEtherNodePatch::MaskBuildingId)
				{
					N.BuildingId = PatchNode->Patch.BuildingId;
					N.DefData = FAsceEtherBuildingDef();
					N.bHasDefData = UAsceEntityDefLibrary::GetEtherBuildingDefByName(N.BuildingId, N.DefData);
					// 同 AddNode 的契约：换建筑类型时行必须存在（ensure 报错，不做重试）
					ensureMsgf(N.bHasDefData,
						TEXT("[Ether] PatchNode 换建筑类型失败：%s（行不存在 / 不是 Ether 建筑 / 表没挂载）——节点将没有端口"),
						*N.BuildingId.ToString());
					N.bLinksInitialized = false;   // 换建筑类型：重新套用新类型的默认内布线
					bNeedRebuild = true; // 整组端口换掉（不搬在制品）
				}
				if (PatchNode->Patch.Mask & FAsceEtherNodePatch::MaskPlacement)
				{
					// **局部**：摆放变了（建筑系统已改好 footprint + 实体 Transform，Ether 这里重读）⇒
					// 端口按新朝向重摆（身份复用 ⇒ 在制品保留）+ 重扫（含**旧位置**挡住/放开的邻居）+ 段重算
					const FAsceGridFootprintFragment OldFootprint = N.CachedFootprint;
					const bool bHadOldFootprint = N.bHasCachedFootprint;
					BuildNodePorts(*NodeId, Ctx);
					TArray<int32> Touched;
					RebuildNodeInternalTopology(*NodeId, Touched);
					TArray<int32> RescanTouched;
					RescanExternalEdgesForNode(*NodeId, RescanTouched, Ctx, bHadOldFootprint ? &OldFootprint : nullptr);
					for (const int32 PortId : RescanTouched)
					{
						Touched.AddUnique(PortId);
					}
					TArray<int32> AllAffected;
					RecomputeSegmentsForPorts(Touched, &AllAffected);
					TArray<int32> DevNodes;
					GatherAdjacentDeviceNodes(AllAffected, DevNodes);
					RecomputeDeviceAdjSegsForNodes(DevNodes);
					bLocalChanged = true;
				}
			}
		}
		else if (const FAsceEtherCmd_SetNodeLinks* SetLinks = Command.GetPtr<FAsceEtherCmd_SetNodeLinks>())
		{
			const FMassEntityHandle Entity = FMassEntityHandle::FromNumber(static_cast<uint64>(SetLinks->EntityNumber));
			if (const int32* NodeId = EntityToNode.Find(Entity))
			{
				// **局部**：只重建该建筑的内布线衍生数据 + 受影响的段（不整图重建）
				Nodes[*NodeId].InternalLinks = SetLinks->Links;
				TArray<int32> Touched;
				RebuildNodeInternalTopology(*NodeId, Touched);
				TArray<int32> AllAffected;
				RecomputeSegmentsForPorts(Touched, &AllAffected);
				TArray<int32> DevNodes;
				GatherAdjacentDeviceNodes(AllAffected, DevNodes);
				RecomputeDeviceAdjSegsForNodes(DevNodes);
				bLocalChanged = true;
			}
		}
		else if (const FAsceEtherCmd_PatchPort* PatchPort = Command.GetPtr<FAsceEtherCmd_PatchPort>())
		{
			const FMassEntityHandle Entity = FMassEntityHandle::FromNumber(static_cast<uint64>(PatchPort->EntityNumber));
			const int32* NodeId = EntityToNode.Find(Entity);
			const int32 PortId = NodeId ? FindPortBySlotKey(*NodeId, PatchPort->Slot) : INDEX_NONE;
			if (Ports.IsValidIndex(PortId) && Ports[PortId].bActive)
			{
				FAsceEtherPortData& P = Ports[PortId];
				// 只有"影响线路"的改动才需要重扫/段重算：射程变化（PortTypeId）或维度变化
				bool bGraphAffected = false;
				if (PatchPort->Patch.Mask & FAsceEtherPortPatch::MaskSplitMode)
				{
					P.SplitMode = PatchPort->Patch.SplitMode; // 纯行为改动：不动图、不用重扫
				}
				if (PatchPort->Patch.Mask & FAsceEtherPortPatch::MaskPortTypeId)
				{
					const int32 OldLinkRange = P.LinkRange;
					P.PortTypeId = PatchPort->Patch.PortTypeId;
					ResolvePortTypeConfig(P); // Op 变化不影响图；射程变化才需要重扫
					if (P.LinkRange != OldLinkRange)
					{
						bGraphAffected = true;
					}
				}
				if (PatchPort->Patch.Mask & FAsceEtherPortPatch::MaskDimension)
				{
					P.Dimension = PatchPort->Patch.Dimension;
					bGraphAffected = true;
				}

				if (bGraphAffected)
				{
					// **局部**：几何/射程可能变了 ⇒ 重扫该建筑的外部边（不含整图重建）
					TArray<int32> Touched;
					Touched.Add(PortId);
					TArray<int32> RescanTouched;
					RescanExternalEdgesForNode(*NodeId, RescanTouched, Ctx);
					for (const int32 Other : RescanTouched)
					{
						Touched.AddUnique(Other);
					}
					TArray<int32> AllAffected;
					RecomputeSegmentsForPorts(Touched, &AllAffected);
					TArray<int32> DevNodes;
					GatherAdjacentDeviceNodes(AllAffected, DevNodes);
					RecomputeDeviceAdjSegsForNodes(DevNodes);
					bLocalChanged = true;
				}
			}
		}
		else if (Command.GetPtr<FAsceEtherCmd_RefreshData>() || Command.GetPtr<FAsceEtherCmd_FullRebuild>())
		{
			bNeedRebuild = true;
		}
	}

	GraphCommands.Reset();

	if (bNeedRebuild)
	{
		RebuildGraphInternal(Ctx);
	}
	else if (bLocalChanged)
	{
		if (bValidateIncremental)
		{
			ValidateIncrementalAgainstFullRebuild(Ctx); // 内部会跑一次全量重建做 diff（并用全量结果兜底）
		}
		else
		{
			// 局部更新完成后：活跃表按"仍有料"重建一次（只扫描段，不做图算法）
			RefreshInitialActiveSegments();
		}
	}
}

uint8 UAsceEtherNetworkSubsystem::GetNodeRotation(FMassEntityHandle Entity, const FAsceTickContext& Ctx) const
{
	const FMassEntityManager* EM = Ctx.EntityManager;
	const FTransformFragment* Transform = EM ? EM->GetFragmentDataPtr<FTransformFragment>(Entity) : nullptr;
	if (!Transform)
	{
		return 0;
	}
	// Yaw → 顺时针 90° 次数（负角度也归一化到 0~3）
	return static_cast<uint8>(((FMath::RoundToInt(Transform->GetTransform().Rotator().Yaw / 90.f) % 4) + 4) % 4);
}

int32 UAsceEtherNetworkSubsystem::FindNodeByEntity(FMassEntityHandle Entity) const
{
	const int32* Found = EntityToNode.Find(Entity);
	return Found ? *Found : INDEX_NONE;
}

int32 UAsceEtherNetworkSubsystem::FindPortBySlotKey(int32 NodeId, const FAsceEtherSlotKey& Slot) const
{
	const int32 Count = GetNodePortCount(NodeId);
	for (int32 i = 0; i < Count; ++i)
	{
		const FAsceEtherPortData* P = GetPort(NodeId, i);
		if (P && P->SlotDirection == Slot.Direction && P->Offset == Slot.Offset && P->Dimension == Slot.Dimension)
		{
			return GetNodePortId(NodeId, i);
		}
	}
	return INDEX_NONE;
}

void UAsceEtherNetworkSubsystem::RemoveNodeInternal(int32 NodeId)
{
	if (!Nodes.IsValidIndex(NodeId) || !Nodes[NodeId].bAlive)
	{
		return;
	}
	EntityToNode.Remove(Nodes[NodeId].Entity);
	// 顺手清掉它残留的"相邻段"表（死节点不该再有邻接；Reset 保容量，O(1)）
	if (DeviceAdjSegs.IsValidIndex(NodeId))
	{
		DeviceAdjSegs[NodeId].Reset();
	}
	// 回收该建筑的端口槽位（在制品随之丢弃）
	for (const int32 PortId : Nodes[NodeId].SlotPortIds)
	{
		FreePortId(PortId);
	}
	Nodes[NodeId].SlotPortIds.Reset();
	Nodes[NodeId].bAlive = false;
	Nodes[NodeId].Entity = FMassEntityHandle();
	Nodes[NodeId].DefData = FAsceEtherBuildingDef();
	Nodes[NodeId].bHasDefData = false;
	Nodes[NodeId].bLinksInitialized = false;
	Nodes[NodeId].InternalLinks.Reset();
	if (NodeCores.IsValidIndex(NodeId))
	{
		NodeCores[NodeId] = nullptr; // 建筑的 core 实例随建筑一起销毁（状态随之丢失）
	}
	FreeIds.Add(NodeId);
}

void UAsceEtherNetworkSubsystem::UnregisterBuilding(int32 NodeId)
{
	if (IsNodeAlive(NodeId))
	{
		PushRemoveNode(Nodes[NodeId].Entity);
	}
}

void UAsceEtherNetworkSubsystem::UnregisterBuildingByEntity(FMassEntityHandle Entity)
{
	PushRemoveNode(Entity);
}

bool UAsceEtherNetworkSubsystem::IsNodeAlive(int32 NodeId) const
{
	return Nodes.IsValidIndex(NodeId) && Nodes[NodeId].bAlive;
}

FMassEntityHandle UAsceEtherNetworkSubsystem::GetEntity(int32 NodeId) const
{
	return IsNodeAlive(NodeId) ? Nodes[NodeId].Entity : FMassEntityHandle();
}

bool UAsceEtherNetworkSubsystem::IsDevice(int32 NodeId) const
{
	return IsNodeAlive(NodeId) && Nodes[NodeId].bDevice;
}

int32 UAsceEtherNetworkSubsystem::AllocSegId()
{
	int32 SegId = INDEX_NONE;
	if (FreeSegIds.Num() > 0)
	{
		SegId = FreeSegIds.Pop(EAllowShrinking::No);
	}
	else
	{
		SegId = Segments.AddDefaulted();
	}
	FAsceEtherSegment& Seg = Segments[SegId];
	Seg.Order.Reset();
	Seg.Stamp = 0;
	Seg.bActive = true;
	return SegId;
}

void UAsceEtherNetworkSubsystem::FreeSegId(int32 SegId)
{
	if (Segments.IsValidIndex(SegId) && Segments[SegId].bActive)
	{
		Segments[SegId].Order.Reset();
		Segments[SegId].Stamp = 0;
		Segments[SegId].bActive = false;
		FreeSegIds.Add(SegId);
	}
}

int32 UAsceEtherNetworkSubsystem::AllocPortId()
{
	int32 PortId = INDEX_NONE;
	if (FreePortIds.Num() > 0)
	{
		PortId = FreePortIds.Pop(EAllowShrinking::No);
	}
	else
	{
		PortId = Ports.AddDefaulted();
		PortSegId.Add(INDEX_NONE);   // 并行数组随端口池增长（不变量：PortSegId.Num() == Ports.Num()）
	}
	PortSegId[PortId] = INDEX_NONE;
	Ports[PortId] = FAsceEtherPortData();  // 重置（运行态清零）
	Ports[PortId].bActive = true;
	return PortId;
}

void UAsceEtherNetworkSubsystem::FreePortId(int32 PortId)
{
	if (Ports.IsValidIndex(PortId) && Ports[PortId].bActive)
	{
		// 不变量：回收前调用方必须已经摘掉它的**出边**（`ClearExternalEdge`）；入边由对端负责
		if (Ports[PortId].OutTargetPort != INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Ether] 回收端口 %d 时它仍有一条外部出边（-> %d）：调用方应先 ClearExternalEdge 再 FreePortId"),
				PortId, Ports[PortId].OutTargetPort);
			ClearExternalEdge(PortId);
		}
		// 内部布线/入边引用由重建或调用方负责，这里只回收槽位
		Ports[PortId] = FAsceEtherPortData();
		if (PortSegId.IsValidIndex(PortId))
		{
			PortSegId[PortId] = INDEX_NONE;
		}
		FreePortIds.Add(PortId);
	}
}

int32 UAsceEtherNetworkSubsystem::GetNodePortCount(int32 NodeId) const
{
	return IsNodeAlive(NodeId) ? Nodes[NodeId].SlotPortIds.Num() : 0;
}

int32 UAsceEtherNetworkSubsystem::GetNodePortId(int32 NodeId, int32 SlotIndex) const
{
	if (!IsNodeAlive(NodeId) || !Nodes[NodeId].SlotPortIds.IsValidIndex(SlotIndex))
	{
		return INDEX_NONE;
	}
	return Nodes[NodeId].SlotPortIds[SlotIndex];
}

UAsceEtherProcessCore* UAsceEtherNetworkSubsystem::GetProcessCore(int32 NodeId) const
{
	return NodeCores.IsValidIndex(NodeId) ? NodeCores[NodeId].Get() : nullptr;
}

const FAsceEtherBuildingDef* UAsceEtherNetworkSubsystem::GetNodeDef(int32 NodeId) const
{
	return (IsNodeAlive(NodeId) && Nodes[NodeId].bHasDefData) ? &Nodes[NodeId].DefData : nullptr;
}

// ── 端口访问 ──

FAsceEtherPortData* UAsceEtherNetworkSubsystem::GetPort(int32 NodeId, int32 LocalIndex)
{
	if (!IsNodeAlive(NodeId))
	{
		return nullptr;
	}
	const int32 PortId = GetNodePortId(NodeId, LocalIndex);
	return (Ports.IsValidIndex(PortId) && Ports[PortId].bActive) ? &Ports[PortId] : nullptr;
}

const FAsceEtherPortData* UAsceEtherNetworkSubsystem::GetPort(int32 NodeId, int32 LocalIndex) const
{
	return const_cast<UAsceEtherNetworkSubsystem*>(this)->GetPort(NodeId, LocalIndex);
}

int32 UAsceEtherNetworkSubsystem::FindPortByTransport(int32 NodeId, EAsceEtherTransportType Want) const
{
	const int32 Count = GetNodePortCount(NodeId);
	for (int32 i = 0; i < Count; ++i)
	{
		if (const FAsceEtherPortData* P = GetPort(NodeId, i))
		{
			if (P->TransportType == Want)
			{
				return i;
			}
		}
	}
	return INDEX_NONE;
}

uint16 UAsceEtherNetworkSubsystem::GetPortEther(int32 NodeId, int32 LocalIndex) const
{
	const FAsceEtherPortData* P = GetPort(NodeId, LocalIndex);
	return P ? P->Ether : 0;
}

int16 UAsceEtherNetworkSubsystem::GetPortSignalType(int32 NodeId, int32 LocalIndex) const
{
	const FAsceEtherPortData* P = GetPort(NodeId, LocalIndex);
	return P ? P->SignalType : 0;
}

void UAsceEtherNetworkSubsystem::SetPort(int32 NodeId, int32 LocalIndex, uint16 Ether, int16 Type)
{
	if (FAsceEtherPortData* P = GetPort(NodeId, LocalIndex))
	{
		P->Ether = Ether;
		P->SignalType = (Ether > 0) ? Type : 0;
	}
}

void UAsceEtherNetworkSubsystem::ClearPort(int32 NodeId, int32 LocalIndex)
{
	if (FAsceEtherPortData* P = GetPort(NodeId, LocalIndex))
	{
		P->Ether = 0;
		P->SignalType = 0;
	}
}

bool UAsceEtherNetworkSubsystem::MintFromEnergy(int32 NodeId, int32 LocalIndex, int16 Type)
{
	FAsceEtherPortData* Out = GetPort(NodeId, LocalIndex);
	if (!Out)
	{
		return false;
	}
	// 0 号（能量）不能靠能量重铸（否则能量就能凭空翻倍）
	const uint16 Cost = AsceEther::Family(Type);
	if (Cost == 0)
	{
		return false;
	}

	const int32 Count = GetNodePortCount(NodeId);
	uint32 EnergyAvail = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		const FAsceEtherPortData* P = GetPort(NodeId, i);
		if (P && P->Ether > 0 && AsceEther::Family(P->SignalType) == 0)
		{
			EnergyAvail += P->Ether;
		}
	}

	const uint16 OutRem = (Out->Throughput > Out->Ether) ? static_cast<uint16>(Out->Throughput - Out->Ether) : 0;
	const bool bTypeOk = (Out->Ether == 0) || (Out->SignalType == Type);
	const uint32 Produce = FMath::Min<uint32>(EnergyAvail / Cost, OutRem);
	if (!bTypeOk || Produce == 0)
	{
		return false;
	}

	uint32 NeedEnergy = Produce * Cost;
	for (int32 i = 0; i < Count && NeedEnergy > 0; ++i)
	{
		FAsceEtherPortData* P = GetPort(NodeId, i);
		if (P && P->Ether > 0 && AsceEther::Family(P->SignalType) == 0)
		{
			const uint16 Take = static_cast<uint16>(FMath::Min<uint32>(P->Ether, NeedEnergy));
			P->Ether = static_cast<uint16>(P->Ether - Take);
			NeedEnergy -= Take;
			if (P->Ether == 0)
			{
				P->SignalType = 0;
			}
		}
	}

	if (Out->Ether == 0)
	{
		Out->SignalType = Type;
	}
	Out->Ether = static_cast<uint16>(Out->Ether + Produce);
	return true;
}

const TArray<FAsceEtherLinkDef>* UAsceEtherNetworkSubsystem::GetInternalLinks(int32 NodeId) const
{
	return IsNodeAlive(NodeId) ? &Nodes[NodeId].InternalLinks : nullptr;
}

void UAsceEtherNetworkSubsystem::SetInternalLinks(int32 NodeId, const TArray<FAsceEtherLinkDef>& Links)
{
	if (!IsNodeAlive(NodeId))
	{
		return;
	}
	Nodes[NodeId].InternalLinks = Links;
	SetBuildingDirty();
}

// ── 图 ──

void UAsceEtherNetworkSubsystem::ClearGraph()
{
	// 段池：段对象回收复用（拓扑序数组清空），不整体 Reset
	for (int32 SegId = 0; SegId < Segments.Num(); ++SegId)
	{
		if (Segments[SegId].bActive)
		{
			FreeSegId(SegId);
		}
	}
	PortSegId.Init(INDEX_NONE, Ports.Num());   // 与端口池等长（端口持久化；增量路径依赖这个不变量）
	DeviceAdjSegs.Reset();
	DeviceNodes.Reset();
	ActiveList.Reset();
	for (TArray<int32>& Shard : ActiveShards)
	{
		Shard.Reset();
	}
	// 注意：**端口实例数组不在这里清空**（端口持久化；只重建图/段/调度这些派生数据）
	for (int32 NodeId = 0; NodeId < Nodes.Num(); ++NodeId)
	{
		FNode& N = Nodes[NodeId];
		if (!N.bAlive)
		{
			N.SlotPortIds.Reset();
			N.InternalLinks.Reset();
			N.DefData = FAsceEtherBuildingDef();
			N.bHasDefData = false;
			if (NodeCores.IsValidIndex(NodeId))
			{
				NodeCores[NodeId] = nullptr;
			}
		}
	}
}

void UAsceEtherNetworkSubsystem::ActivateSeg(int32 SegId)
{
	if (!Segments.IsValidIndex(SegId))
	{
		return;
	}
	FAsceEtherSegment& Seg = Segments[SegId];

	// 原子去重：只有"把 Stamp 从旧值换成 CurrentStamp"的那个调用者负责入列（并行安全）
	const uint32 Prev = static_cast<uint32>(FPlatformAtomics::InterlockedExchange(
		reinterpret_cast<volatile int32*>(&Seg.Stamp), static_cast<int32>(CurrentStamp)));
	if (Prev == CurrentStamp)
	{
		return; // 本拍已入列
	}

	if (ActiveShards.Num() == 0)
	{
		ActiveShards.SetNum(GetActiveShardNum());
	}
	// 每线程一个分片索引（首次使用时分配；分片数 ≥ 线程数 ⇒ 不会两个线程撞同一分片）
	static thread_local int32 ShardIndex = INDEX_NONE;
	if (ShardIndex == INDEX_NONE)
	{
		ShardIndex = FPlatformAtomics::InterlockedIncrement(&ShardAssignCounter) - 1;
	}
	ActiveShards[ShardIndex % ActiveShards.Num()].Add(SegId);
}

void UAsceEtherNetworkSubsystem::MergeActiveShards()
{
	for (TArray<int32>& Shard : ActiveShards)
	{
		if (Shard.Num() > 0)
		{
			ActiveList.Append(Shard);
			Shard.Reset();
		}
	}
}

void UAsceEtherNetworkSubsystem::RebuildGraphInternal(const FAsceTickContext& Ctx)
{
	// ① 端口**持久化**：端口数组不再重建，按"相对几何身份"原地复用（id 稳定、在制品不动）⇒ 不需要快照
	// ② 连线扫描上限由**端口类型**的 `LinkRangeCells` 决定（只看发射端；0 = 不能连）

	ClearGraph();

	// 全端口射程最大值：全量重建时精确重算（端口类型解析时维护，增量路径只增不减）
	MaxPortLinkRange = 0;

	const int32 NodeNum = Nodes.Num();
	DeviceAdjSegs.SetNum(NodeNum);
	NodeCores.SetNum(NodeNum);
	if (NodeNum == 0)
	{
		return;
	}

	const FMassEntityManager* EM = Ctx.EntityManager;
	if (!EM)
	{
		return;
	}

	TArray<TArray<int32>> NodePortIds; // nodeId -> 全局 PortId
	NodePortIds.SetNum(NodeNum);

	// ③ 端口实例：slot → 端口（**持久化 + 身份复用**；实现在 `BuildNodePorts`，增量路径共用）
	for (int32 NodeId = 0; NodeId < NodeNum; ++NodeId)
	{
		if (!Nodes[NodeId].bAlive)
		{
			continue;
		}
		BuildNodePorts(NodeId, Ctx);
		NodePortIds[NodeId] = Nodes[NodeId].SlotPortIds;
	}

	// ④ 内部布线 → 后继（LinkTargets）
	for (int32 NodeId = 0; NodeId < NodeNum; ++NodeId)
	{
		if (!IsNodeAlive(NodeId) || NodePortIds[NodeId].Num() == 0)
		{
			continue;
		}
		const TArray<int32>& PortIds = NodePortIds[NodeId];
		for (const FAsceEtherLinkDef& LD : Nodes[NodeId].InternalLinks)
		{
			if (LD.FromPort == LD.ToPort)
			{
				continue; // 不自连
			}
			if (!PortIds.IsValidIndex(LD.FromPort) || !PortIds.IsValidIndex(LD.ToPort))
			{
				UE_LOG(LogTemp, Warning, TEXT("[Ether] InternalLinks 下标越界：%s (%d -> %d)"),
					*Nodes[NodeId].BuildingId.ToString(), LD.FromPort, LD.ToPort);
				continue;
			}
			const int32 FromPort = PortIds[LD.FromPort];
			const int32 ToPort = PortIds[LD.ToPort];
			// 并行前提：内部布线两端必须同建筑（同建筑 ⇒ 同线程 / 同段）
			if (Ports[FromPort].NodeId != Ports[ToPort].NodeId)
			{
				UE_LOG(LogTemp, Error, TEXT("[Ether] 断言失败：InternalLinks 跨建筑（%d -> %d）"), FromPort, ToPort);
				continue;
			}
			Ports[FromPort].LinkTargets.AddUnique(ToPort);
		}
	}

	// 分流/背压顺序（§2.9）：相对源端口，从它左手边起顺时针；同方向内 Dim 低→高；再按格坐标 / PortId 兜底（Sort 不稳定）
	for (int32 PortId = 0; PortId < Ports.Num(); ++PortId)
	{
		FAsceEtherPortData& P = Ports[PortId];
		if (!P.bActive || P.LinkTargets.Num() <= 1)
		{
			continue; // 空闲槽位 / 0~1 个目标都不需要排序
		}
		const int32 SrcDir = static_cast<int32>(P.Direction);
		P.LinkTargets.Sort([this, SrcDir](int32 A, int32 B)
		{
			const FAsceEtherPortData& PA = Ports[A];
			const FAsceEtherPortData& PB = Ports[B];
			const int32 RA = (static_cast<int32>(PA.Direction) - SrcDir + 1) & 3;
			const int32 RB = (static_cast<int32>(PB.Direction) - SrcDir + 1) & 3;
			if (RA != RB)
			{
				return RA < RB;
			}
			if (PA.Dimension != PB.Dimension)
			{
				return PA.Dimension < PB.Dimension;
			}
			if (PA.CellY != PB.CellY)
			{
				return PA.CellY < PB.CellY;
			}
			if (PA.CellX != PB.CellX)
			{
				return PA.CellX < PB.CellX;
			}
			return A < B;
		});
	}

	// ⑤ 行为：创建/复用 ProcessCore（`EnsureNodeCore`，增量路径共用）+ 收集器件列表
	DeviceNodes.Reset();
	for (int32 NodeId = 0; NodeId < NodeNum; ++NodeId)
	{
		if (!IsNodeAlive(NodeId))
		{
			continue;
		}
		if (Nodes[NodeId].bDevice)
		{
			DeviceNodes.Add(NodeId); // 升序（Pass 1/2 只遍历它）
		}
		EnsureNodeCore(NodeId);
	}

	// ⑥ 定向 + 外部边（`TransportType` 只在这里用一次；tick 推流完全不看）——重扫用 `RayScanPort`
	for (int32 PortId = 0; PortId < Ports.Num(); ++PortId)
	{
		FAsceEtherPortData& SPort = Ports[PortId];
		if (!SPort.bActive || !IsEmittingPort(SPort))
		{
			continue;
		}
		const int32 ToPort = RayScanPort(PortId, Ctx.Grid);
		if (ToPort != INDEX_NONE)
		{
			// 外部边是 1:1：直接记对端 + 反向索引（同一目标最多一条入边，由建图规则保证）
			if (Ports[ToPort].InSourcePort != INDEX_NONE)
			{
				UE_LOG(LogTemp, Error, TEXT("[Ether] 断言失败：端口 %d 已有外部入边（来自 %d），又来一条（来自 %d）"),
					ToPort, Ports[ToPort].InSourcePort, PortId);
			}
			SPort.OutTargetPort = ToPort;
			Ports[ToPort].InSourcePort = PortId;
		}
	}

	// ⑦ 入边源表（内部布线目标 + 唯一的外部入边来源）+ And/Or 记录数组
	for (int32 PortId = 0; PortId < Ports.Num(); ++PortId)
	{
		FAsceEtherPortData& P = Ports[PortId];
		if (!P.bActive)
		{
			continue;
		}
		for (const int32 ToPort : P.LinkTargets)
		{
			Ports[ToPort].InSources.AddUnique(PortId);
		}
		if (P.InSourcePort != INDEX_NONE)
		{
			P.InSources.AddUnique(P.InSourcePort);
		}
	}
	// 入边列表规范为升序（pull 顺序确定）
	for (FAsceEtherPortData& P : Ports)
	{
		if (!P.bActive)
		{
			continue;
		}
		if (P.InSources.Num() > 1)
		{
			P.InSources.Sort();
		}
	}

	// ⑧ 组合段（端口级）：组合端口 = 非器件节点的端口；段内 Kahn 拓扑序
	PortSegId.Init(INDEX_NONE, Ports.Num());

	auto IsCombPort = [&](int32 PortId)
	{
		const int32 NodeId = Ports[PortId].NodeId;
		return IsNodeAlive(NodeId) && !Nodes[NodeId].bDevice;
	};

	TArray<TArray<int32>> CombUndir;
	TArray<TArray<int32>> CombOut;
	CombUndir.SetNum(Ports.Num());
	CombOut.SetNum(Ports.Num());
	for (int32 PortId = 0; PortId < Ports.Num(); ++PortId)
	{
		if (!IsCombPort(PortId))
		{
			continue;
		}
		for (const int32 ToPort : Ports[PortId].LinkTargets)
		{
			if (IsCombPort(ToPort))
			{
				CombOut[PortId].Add(ToPort);
				CombUndir[PortId].Add(ToPort);
				CombUndir[ToPort].Add(PortId);
			}
		}
	}
	for (int32 PortId = 0; PortId < Ports.Num(); ++PortId)
	{
		if (!IsCombPort(PortId))
		{
			continue;
		}
		const int32 ToPort = Ports[PortId].OutTargetPort;
		if (ToPort != INDEX_NONE && IsCombPort(ToPort))
		{
			CombOut[PortId].Add(ToPort);
			CombUndir[PortId].Add(ToPort);
			CombUndir[ToPort].Add(PortId);
		}
	}

	TArray<uint8> Visited;
	Visited.Init(0, Ports.Num());

	for (int32 Seed = 0; Seed < Ports.Num(); ++Seed)
	{
		if (!IsCombPort(Seed) || Visited[Seed])
		{
			continue;
		}

		// BFS 收集本分量
		TArray<int32> Comp;
		Comp.Add(Seed);
		Visited[Seed] = 1;
		for (int32 Head = 0; Head < Comp.Num(); ++Head)
		{
			const int32 U = Comp[Head];
			for (const int32 V : CombUndir[U])
			{
				if (!Visited[V])
				{
					Visited[V] = 1;
					Comp.Add(V);
				}
			}
		}

		// 段内拓扑排序（Kahn）
		TMap<int32, int32> InDeg;
		InDeg.Reserve(Comp.Num());
		for (const int32 U : Comp)
		{
			InDeg.Add(U, 0);
		}
		for (const int32 U : Comp)
		{
			for (const int32 V : CombOut[U])
			{
				if (int32* D = InDeg.Find(V))
				{
					++(*D);
				}
			}
		}
		TArray<int32> Queue;
		for (const int32 U : Comp)
		{
			if (InDeg[U] == 0)
			{
				Queue.Add(U);
			}
		}
		TArray<int32> Order;
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 U = Queue[Head];
			Order.Add(U);
			for (const int32 V : CombOut[U])
			{
				if (int32* D = InDeg.Find(V))
				{
					if (--(*D) == 0)
					{
						Queue.Add(V);
					}
				}
			}
		}
		if (Order.Num() != Comp.Num())
		{
			// 组合环（无器件破环）→ 非法：环上端口不参与运算（避免"带着环跑"产生不确定结果）
			UE_LOG(LogTemp, Error, TEXT("[Ether] 组合环：段内 %d 个端口只排出 %d 个（环上端口已拒绝处理，应由器件破环）"),
				Comp.Num(), Order.Num());
		}

		const int32 SegId = AllocSegId();
		FAsceEtherSegment& Seg = Segments[SegId];
		Seg.Order = MoveTemp(Order);
		for (const int32 U : Seg.Order)
		{
			PortSegId[U] = SegId;
		}
	}

	// ⑨ 器件 node -> 相邻段
	auto LinkDeviceSeg = [&](int32 PortA, int32 PortB)
	{
		const bool bAComb = IsCombPort(PortA);
		const bool bBComb = IsCombPort(PortB);
		if (bAComb == bBComb)
		{
			return;
		}
		const int32 CombPort = bAComb ? PortA : PortB;
		const int32 DevNode = Ports[bAComb ? PortB : PortA].NodeId;
		const int32 Seg = PortSegId[CombPort];
		if (Seg != INDEX_NONE && DeviceAdjSegs.IsValidIndex(DevNode))
		{
			DeviceAdjSegs[DevNode].AddUnique(Seg);
		}
	};
	for (int32 PortId = 0; PortId < Ports.Num(); ++PortId)
	{
		for (const int32 ToPort : Ports[PortId].LinkTargets)
		{
			LinkDeviceSeg(PortId, ToPort);
		}
	}
	for (int32 PortId = 0; PortId < Ports.Num(); ++PortId)
	{
		if (!Ports[PortId].bActive)
		{
			continue;
		}
		const int32 ToPort = Ports[PortId].OutTargetPort;
		if (ToPort != INDEX_NONE)
		{
			LinkDeviceSeg(PortId, ToPort);
		}
	}

	// ⑩ 初始活跃段：重建后仍有残留的段直接进活跃表（段 Stamp 用本拍值；ActivateSeg 会因 Stamp 相同而跳过）
	RefreshInitialActiveSegments();
}

// ── 每 tick ──

void UAsceEtherNetworkSubsystem::RunCommandDrainStage(const FAsceTickContext& Ctx)
{
	// 结构变化只通过命令队列进来；本阶段是**唯一 drain 点**（排在空间数据重建之后，图在本拍内冻结）
	++CurrentStamp;
	if (GraphCommands.Num() > 0)
	{
		ApplyGraphCommands(Ctx);
	}
}

void UAsceEtherNetworkSubsystem::RunSimulationStage(const FAsceTickContext& Ctx)
{
	if (Ports.Num() == 0)
	{
		return;
	}

	// Pass 1：时序器件的 core 执行（读入端口 → 更新自己的状态 → 写自己的端口缓冲）
	// 只遍历 DeviceNodes（升序缓存）；激活写进分片，Pass 3 前统一合并
	// 并行前提：core 只读写自己建筑的端口 / 自己的成员，不碰共享状态（见 .h 契约）
	{
		const int32 DeviceNum = DeviceNodes.Num();
		auto SampleDevice = [this](int32 Index)
		{
			const int32 NodeId = DeviceNodes[Index];
			FNode& N = Nodes[NodeId];
			if (!N.bAlive || !N.bDevice)
			{
				return;
			}
			if (UAsceEtherProcessCore* Core = GetProcessCore(NodeId))
			{
				Core->Tick(*this, NodeId);
			}
			// 本建筑端口上有料 ⇒ 激活相邻段（让下游组合段本拍就能传播）
		for (int32 i = 0; i < N.SlotPortIds.Num(); ++i)
		{
			const FAsceEtherPortData& P = Ports[N.SlotPortIds[i]];
			if (P.bActive && P.Ether > 0)
			{
				for (const int32 Seg : DeviceAdjSegs[NodeId])
				{
					ActivateSeg(Seg);
				}
				break;
			}
		}
		};
		if (DeviceNum >= EtherParallelThreshold)
		{
			ParallelFor(DeviceNum, SampleDevice);
		}
		else
		{
			for (int32 i = 0; i < DeviceNum; ++i)
			{
				SampleDevice(i);
			}
		}
	}

	// Pass 2：把器件端口推出去（含上拍残留；保证"器件 → 器件"也是 1 tick/级，与 nodeId 顺序无关）
	// 并行前提：外部边每个目标端口 ≤1 条（建图已断言）⇒ 各器件写的目标互不重叠；内部布线目标都在自己建筑里
	{
		const int32 DeviceNum = DeviceNodes.Num();
		auto PushDevicePorts = [this](int32 Index)
		{
			const int32 NodeId = DeviceNodes[Index];
			const FNode& N = Nodes[NodeId];
			if (!N.bAlive || !N.bDevice)
			{
				return;
			}
		for (int32 i = 0; i < N.SlotPortIds.Num(); ++i)
		{
			PushPort(N.SlotPortIds[i]);
		}
		};
		if (DeviceNum >= EtherParallelThreshold)
		{
			ParallelFor(DeviceNum, PushDevicePorts);
		}
		else
		{
			for (int32 i = 0; i < DeviceNum; ++i)
			{
				PushDevicePorts(i);
			}
		}
	}

	// 合并 Pass 1 期间写入的分片（此后 ActiveList 才是完整的本拍活跃段集合）
	MergeActiveShards();

	// Pass 3：组合传播（段内拓扑序；**段间并行**）
	// 并行前提：段与段不共享端口（跨段边只连到器件端口，而器件端口最多一条外部入边）
	{
		const int32 ActiveNum = ActiveList.Num();
		TArray<uint8> Survived;
		Survived.SetNumZeroed(ActiveNum);
		auto ProcessSeg = [this, &Survived](int32 Index)
		{
			const int32 SegId = ActiveList[Index];
			const FAsceEtherSegment& Seg = Segments[SegId];
			bool bStillHasEther = false;
			for (const int32 PortId : Seg.Order)
			{
				ProcessPort(PortId, bStillHasEther);
			}
			Survived[Index] = bStillHasEther ? 1 : 0;
		};
		if (ActiveNum >= EtherParallelThreshold)
		{
			ParallelFor(ActiveNum, ProcessSeg);
		}
		else
		{
			for (int32 i = 0; i < ActiveNum; ++i)
			{
				ProcessSeg(i);
			}
		}

		// 串行收尾：存活段留到下一拍（stamp = CurrentStamp + 1，ActivateSeg 会跳过 ⇒ 不重复入列），其余移除
		int32 Write = 0;
		for (int32 i = 0; i < ActiveNum; ++i)
		{
			const int32 SegId = ActiveList[i];
			if (Survived[i])
			{
				Segments[SegId].Stamp = CurrentStamp + 1;
				ActiveList[Write++] = SegId;
			}
		}
		ActiveList.SetNum(Write, EAllowShrinking::No);
	}
}

void UAsceEtherNetworkSubsystem::PushPort(int32 PortId)
{
	if (!Ports.IsValidIndex(PortId))
	{
		return;
	}
	FAsceEtherPortData& P = Ports[PortId];
	if (P.Ether == 0)
	{
		return;
	}

	// 目标端口是"组合逻辑端口"（And/Or）时不推入：料留在本端口，等逻辑端口处理时自己 pull（§5）
	const auto IsLogicTarget = [this](int32 TargetPortId)
	{
		return IsCombinationalPort(TargetPortId)
			&& (Ports[TargetPortId].Op == EAsceEtherPortOp::And || Ports[TargetPortId].Op == EAsceEtherPortOp::Or);
	};

	// Not = 推出时取互补态；And/Or 的结果符号由 PullAndEvaluate 统一
	const int16 OutType = (P.Op == EAsceEtherPortOp::Not) ? AsceEther::Complement(P.SignalType) : P.SignalType;
	// ① 内部分流（LinkTargets 已按 §2.9 扫描序排好）
	if (P.LinkTargets.Num() > 0)
	{
		if (P.SplitMode == EAsceEtherSplitMode::Balanced)
		{
			// 平均器：在"剩余可接收目标"间均分（ceil ⇒ 余数靠前多分；收不下的份额自动顺延给后面）
			TArray<int32> Recv;
			Recv.Reserve(P.LinkTargets.Num());
			for (const int32 ToPort : P.LinkTargets)
			{
				if (IsLogicTarget(ToPort))
				{
					continue; // 逻辑端口等自己 pull
				}
				const FAsceEtherPortData& Q = Ports[ToPort];
				const bool bFamilyOk = (Q.Ether == 0) || AsceEther::IsSameFamily(Q.SignalType, OutType);
				const uint16 Rem = (Q.Throughput > Q.Ether) ? static_cast<uint16>(Q.Throughput - Q.Ether) : 0;
				if (bFamilyOk && Rem > 0)
				{
					Recv.Add(ToPort);
				}
			}
			for (int32 i = 0; i < Recv.Num() && P.Ether > 0; ++i)
			{
				const int32 Left = Recv.Num() - i;
				const uint16 Share = static_cast<uint16>((P.Ether + Left - 1) / Left); // ceil(剩余 / 剩余目标数)
				MoveUnits(PortId, Recv[i], FMath::Min<uint16>(Share, P.Ether), OutType);
			}
		}
		else // Ordered：灌满一个再下一个
		{
			for (const int32 ToPort : P.LinkTargets)
			{
				if (P.Ether == 0)
				{
					break;
				}
				if (IsLogicTarget(ToPort))
				{
					continue; // 逻辑端口等自己 pull
				}
				MoveUnits(PortId, ToPort, P.Ether, OutType);
			}
		}
	}

	// ② 外部推送（1:1）
	if (P.Ether > 0 && P.OutTargetPort != INDEX_NONE && !IsLogicTarget(P.OutTargetPort))
	{
		MoveUnits(PortId, P.OutTargetPort, P.Ether, OutType);
	}
}

void UAsceEtherNetworkSubsystem::ProcessPort(int32 PortId, bool& bOutHasEther)
{
	PullAndEvaluate(PortId);
	PushPort(PortId);
	if (Ports[PortId].Ether > 0)
	{
		bOutHasEther = true;
	}
}

void UAsceEtherNetworkSubsystem::PullAndEvaluate(int32 PortId)
{
	FAsceEtherPortData& P = Ports[PortId];
	if (P.Op != EAsceEtherPortOp::And && P.Op != EAsceEtherPortOp::Or)
	{
		return; // Wire / Not 不做多输入求值
	}

	// ① 扫描（不消费）：收集"有料入边"的符号 + 异族检测（空入边 = 未定值，不计入）
	bool bHasInput = false;
	bool bAnyPlus = false;
	bool bAnyMinus = false;
	int16 BasePos = 0;
	for (const int32 SrcPort : P.InSources)
	{
		if (!Ports.IsValidIndex(SrcPort))
		{
			continue;
		}
		const FAsceEtherPortData& S = Ports[SrcPort];
		if (!S.bActive || S.Ether == 0)
		{
			continue;
		}
		const int16 T = S.SignalType;
		if (!bHasInput)
		{
			bHasInput = true;
			BasePos = AsceEther::IsComplement(T) ? AsceEther::Complement(T) : T;
		}
		else if (!AsceEther::IsSameFamily(BasePos, T))
		{
			return; // 异族 → 本拍不动作（堵死；料原样留在源端）
		}
		if (AsceEther::Family(T) != 0)
		{
			if (AsceEther::IsComplement(T))
			{
				bAnyMinus = true;
			}
			else
			{
				bAnyPlus = true;
			}
		}
	}
	if (!bHasInput)
	{
		return; // 无输入 ⇒ 不输出（Empty ≠ 逻辑 0）
	}
	// 本端口残留族必须与入边同族（否则同样堵死）
	if (P.Ether > 0 && !AsceEther::IsSameFamily(P.SignalType, BasePos))
	{
		return;
	}

	// ② 合流（pull）：按 `InSources` 顺序从各源端口取料进本端口缓冲（不做湮灭；上限=本端口吞吐）
	for (const int32 SrcPort : P.InSources)
	{
		if (P.Ether >= P.Throughput)
		{
			break;
		}
		if (!Ports.IsValidIndex(SrcPort))
		{
			continue;
		}
		FAsceEtherPortData& S = Ports[SrcPort];
		if (!S.bActive || S.Ether == 0)
		{
			continue;
		}
		const uint16 Move = FMath::Min<uint16>(S.Ether, static_cast<uint16>(P.Throughput - P.Ether));
		if (Move == 0)
		{
			break;
		}
		S.Ether = static_cast<uint16>(S.Ether - Move);
		if (S.Ether == 0)
		{
			S.SignalType = 0;
		}
		P.Ether = static_cast<uint16>(P.Ether + Move);
	}
	if (P.Ether == 0)
	{
		return;
	}

	// ③ 统一符号：And = 全 + 才 +，否则 −；Or = 有 + 则 +，否则 −；0 号（能量）逻辑透明 ⇒ 0
	int16 ResType;
	if (AsceEther::Family(BasePos) == 0)
	{
		ResType = 0;
	}
	else
	{
		const bool bPlus = (P.Op == EAsceEtherPortOp::And) ? !bAnyMinus : bAnyPlus;
		ResType = bPlus ? BasePos : AsceEther::Complement(BasePos);
	}
	P.SignalType = ResType; // 份数照收，只统一符号（§2.7）
}

uint16 UAsceEtherNetworkSubsystem::MoveUnits(int32 FromPort, int32 ToPort, uint16 Amount, int16 Type)
{
	if (Amount == 0 || !Ports.IsValidIndex(FromPort) || !Ports.IsValidIndex(ToPort))
	{
		return 0;
	}
	FAsceEtherPortData& S = Ports[FromPort];
	FAsceEtherPortData& T = Ports[ToPort];
	const uint16 Before = S.Ether;
	uint16 Left = Amount;

	// 目标已有料：先判族（异族 → 堵死：不消费也不输出）
	if (T.Ether > 0)
	{
		if (!AsceEther::IsSameFamily(T.SignalType, Type))
		{
			return 0;
		}
		if (AsceEther::IsComplement(T.SignalType) != AsceEther::IsComplement(Type))
		{
			if (T.InSources.Num() > 1)
			{
				// 合流节点（结构上多条入边）：同族异号 → 1:1 湮灭
				const uint16 Kill = FMath::Min<uint16>(Left, T.Ether);
				S.Ether = static_cast<uint16>(S.Ether - Kill);
				T.Ether = static_cast<uint16>(T.Ether - Kill);
				Left = static_cast<uint16>(Left - Kill);
			}
			else
			{
				// 单一来源的普通链路：符号直接翻转（清空旧残料、采用新料；值变化即时响应，§2.7）
				T.Ether = 0;
			}
		}
	}

	// 目标空 / 同族同号 → 累加（受吞吐上限）
	if (Left > 0)
	{
		const uint16 Rem = (T.Throughput > T.Ether) ? static_cast<uint16>(T.Throughput - T.Ether) : 0;
		const uint16 Cap = FMath::Min<uint16>(Rem, FMath::Min<uint16>(S.Throughput, T.Throughput));
		const uint16 Move = FMath::Min<uint16>(Left, Cap);
		if (Move > 0)
		{
			if (T.Ether == 0)
			{
				T.SignalType = Type;
			}
			S.Ether = static_cast<uint16>(S.Ether - Move);
			T.Ether = static_cast<uint16>(T.Ether + Move);
		}
	}

	const uint16 Processed = static_cast<uint16>(Before - S.Ether); // 本端口"处理掉"的数量（含被湮灭的）

	// 注意：逻辑端口（And/Or）不会被推入（见 PushPort），所以这里没有逻辑目标的特判/符号记录
	return Processed;
}

// ── 调试 ──

bool UAsceEtherNetworkSubsystem::GetPortView(int32 PortId, FPortView& OutView) const
{
	if (!Ports.IsValidIndex(PortId) || !Ports[PortId].bActive)
	{
		return false;
	}
	const FAsceEtherPortData& P = Ports[PortId];
	OutView.NodeId = P.NodeId;
	OutView.Dir = P.Direction;
	OutView.Dim = P.Dimension;
	OutView.Offset = P.Offset;
	OutView.TransportType = P.TransportType;
	OutView.CellX = P.CellX;
	OutView.CellY = P.CellY;
	OutView.Throughput = P.Throughput;
	OutView.Ether = P.Ether;
	OutView.SignalType = P.SignalType;
	OutView.OutTargetPort = P.OutTargetPort;
	return true;
}

const FAsceEtherPortIdList* UAsceEtherNetworkSubsystem::GetPortLinkTargets(int32 PortId) const
{
	return Ports.IsValidIndex(PortId) ? &Ports[PortId].LinkTargets : nullptr;
}
