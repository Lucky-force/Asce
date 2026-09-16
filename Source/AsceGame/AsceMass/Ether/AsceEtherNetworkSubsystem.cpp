// AsceEtherNetworkSubsystem.cpp
#include "AsceEtherNetworkSubsystem.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "AsceMass/Fragments/AsceFootprintFragment.h"
#include "AsceMass/Ether/AsceEtherFragments.h"
#include "AsceMass/FlowField/AscePathFindingSubsystem.h"
#include "AsceMass/FlowField/AscePathFinder.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceEtherNetworkSubsystem)

void UAsceEtherNetworkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAsceEtherNetworkSubsystem::Deinitialize()
{
	ClearGraph();
	BuildingDataRegistry.Reset();
	Nodes.Reset();
	FreeIds.Reset();
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
	// 客户端不参与逻辑（表现后续从复制数据来）：不创建实例，避免空转/误用
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
	Super::Tick(DeltaTime);

	// 只在服务器权威端跑逻辑
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	RunEtherTick();
}

// ── DA 注册表 ──

void UAsceEtherNetworkSubsystem::RegisterBuildingData(FName DefId, const UAsceEtherBuildingData* Data)
{
	if (DefId.IsNone() || !Data)
	{
		return;
	}
	BuildingDataRegistry.Add(DefId, const_cast<UAsceEtherBuildingData*>(Data));
}

const UAsceEtherBuildingData* UAsceEtherNetworkSubsystem::FindBuildingData(FName DefId) const
{
	const TWeakObjectPtr<UAsceEtherBuildingData>* Found = BuildingDataRegistry.Find(DefId);
	return Found ? Found->Get() : nullptr;
}

// ── registry ──

int32 UAsceEtherNetworkSubsystem::AllocNodeId()
{
	if (FreeIds.Num() > 0)
	{
		return FreeIds.Pop(EAllowShrinking::No);
	}
	return Nodes.AddDefaulted();
}

bool UAsceEtherNetworkSubsystem::GetPortView(int32 PortId, FPortView& OutView) const
{
	if (!Ports.IsValidIndex(PortId))
	{
		return false;
	}
	const FPort& P = Ports[PortId];
	OutView.NodeId = P.NodeId;
	OutView.Dir = P.Dir;
	OutView.Dim = P.Dim;
	OutView.CellX = P.CellX;
	OutView.CellY = P.CellY;
	OutView.Throughput = P.Throughput;
	OutView.Ether = P.Ether;
	OutView.SignalType = P.SignalType;
	OutView.bIsInput = P.bIsInput;
	OutView.OutTargetPort = (P.OutEdge != INDEX_NONE && Edges.IsValidIndex(P.OutEdge))
		? Edges[P.OutEdge].ToPort : INDEX_NONE;
	return true;
}

const TArray<int32>* UAsceEtherNetworkSubsystem::GetPortLinkTargets(int32 PortId) const
{
	return Ports.IsValidIndex(PortId) ? &Ports[PortId].LinkTargets : nullptr;
}

const FEtherDeviceStateFragment* UAsceEtherNetworkSubsystem::GetDeviceState(int32 NodeId) const
{
	if (!IsDevice(NodeId))
	{
		return nullptr;
	}
	const UWorld* World = GetWorld();
	UMassEntitySubsystem* MassSub = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!MassSub)
	{
		return nullptr;
	}
	const FMassEntityManager& EM = MassSub->GetEntityManager();
	return EM.GetFragmentDataPtr<FEtherDeviceStateFragment>(Nodes[NodeId].Entity);
}

int32 UAsceEtherNetworkSubsystem::RegisterBuilding(FMassEntityHandle Entity, FName BuildingDefId)
{
	const int32 NodeId = AllocNodeId();
	FNode& N = Nodes[NodeId];
	N.Entity = Entity;
	N.BuildingDefId = BuildingDefId;
	N.bAlive = true;
	EntityToNode.Add(Entity, NodeId);

	if (!FindBuildingData(BuildingDefId))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Ether] 建筑类型数据未登记：%s（检查 Trait 的 BuildingData / BuildingDefId）"),
			*BuildingDefId.ToString());
	}

	// 结构变化：置脏标记，真正重建延迟到下一次 RunEtherTick 开头（批量放置只重建一次）
	bGraphDirty = true;
	return NodeId;
}

void UAsceEtherNetworkSubsystem::UnregisterBuilding(int32 NodeId)
{
	if (!Nodes.IsValidIndex(NodeId) || !Nodes[NodeId].bAlive)
	{
		return;
	}
	EntityToNode.Remove(Nodes[NodeId].Entity);
	Nodes[NodeId].bAlive = false;
	Nodes[NodeId].Entity = FMassEntityHandle();
	Nodes[NodeId].RecipeInputs.Reset();
	FreeIds.Add(NodeId);

	// 结构变化：置脏标记，延迟到下一次 RunEtherTick 开头统一重建
	bGraphDirty = true;
}

void UAsceEtherNetworkSubsystem::UnregisterBuildingByEntity(FMassEntityHandle Entity)
{
	if (const int32* NodeId = EntityToNode.Find(Entity))
	{
		UnregisterBuilding(*NodeId);
	}
}

void UAsceEtherNetworkSubsystem::ClearGraph()
{
	Edges.Reset();
	Links.Reset();
	ProcessOrder.Reset();
	Segments.Reset();
	PortSegId.Reset();
	DeviceAdjSegs.Reset();
	ActiveList.Reset();
	Ports.Reset();
	for (FNode& N : Nodes)
	{
		N.PortStart = 0;
		N.PortCount = 0;
	}
	CurrentStamp = 0;
}

bool UAsceEtherNetworkSubsystem::ActivateSeg(int32 SegId)
{
	if (!Segments.IsValidIndex(SegId) || Segments[SegId].Stamp == CurrentStamp)
	{
		return false;
	}
	Segments[SegId].Stamp = CurrentStamp;
	ActiveList.Add(SegId);
	return true;
}

// ── 建图 / 重建 ──

void UAsceEtherNetworkSubsystem::RebuildGraph()
{
	bGraphDirty = false;

	// 保存旧运行态（按 (nodeId, 建筑内端口下标) 映射），重建后恢复：
	// 结构变化（放置/拆除其它建筑）不应该把全网的在制品和端口缓冲清空。
	TMap<uint64, TPair<uint16, int16>> SavedPortState;
	for (int32 NodeId = 0; NodeId < Nodes.Num(); ++NodeId)
	{
		const FNode& N = Nodes[NodeId];
		if (!N.bAlive)
		{
			continue;
		}
		for (int32 li = 0; li < N.PortCount; ++li)
		{
			const int32 Pid = N.PortStart + li;
			if (!Ports.IsValidIndex(Pid))
			{
				break;
			}
			const FPort& P = Ports[Pid];
			if (P.Ether > 0)
			{
				SavedPortState.Add(
					(static_cast<uint64>(static_cast<uint32>(NodeId)) << 32) | static_cast<uint32>(li),
					TPair<uint16, int16>(P.Ether, P.SignalType));
			}
		}
	}

	// 连线射线扫描上限：至少 MaxLinkRangeCells，实际取"地图长边"，避免大地图上静默断连
	int32 MaxScanCells = AsceEther::MaxLinkRangeCells;
	if (UAscePathFindingSubsystem* PathFinding = UAscePathFindingSubsystem::Get(this))
	{
		if (const UPathFinder* Finder = PathFinding->GetPathFinder())
		{
			if (const FWorldMapData* Map = Finder->GetWorldMapData())
			{
				MaxScanCells = FMath::Max(MaxScanCells, FMath::Max(Map->GetGridCountX(), Map->GetGridCountY()));
			}
		}
	}

	ClearGraph();

	const int32 NodeNum = Nodes.Num();
	DeviceAdjSegs.SetNum(NodeNum);
	if (NodeNum == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	UMassEntitySubsystem* MassSub = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	const FMassEntityManager* EM = MassSub ? &MassSub->GetEntityManager() : nullptr;
	if (!EM)
	{
		return;
	}

	TArray<TArray<int32>> NodePortIds;   // nodeId -> 全局 PortId（本轮分配结果）
	NodePortIds.SetNum(NodeNum);

	// 占用图：格 -> nodeId（footprint 互不重叠）
	TMap<FIntPoint, int32> Occupancy;
	Occupancy.Reserve(NodeNum * 4);

	// ① 端口运行态：从 DA 解析（变长，数量不写死）+ 收集占用格
	for (int32 NodeId = 0; NodeId < NodeNum; ++NodeId)
	{
		if (!Nodes[NodeId].bAlive)
		{
			continue;
		}
		FNode& N = Nodes[NodeId];
		N.PortStart = 0;
		N.PortCount = 0;
		N.bDevice = false;
		N.DeviceOp = 0;
		N.DeviceParamSignal = 1;
		N.DeviceParamA = 1;
		N.DeviceInputPorts.Reset();
		N.DeviceOutputPorts.Reset();
		N.RecipeInputs.Reset();
		N.RecipeOutput = FAsceEtherRecipeItem();

		const FAsceGridFootprintFragment* Foot = EM->GetFragmentDataPtr<FAsceGridFootprintFragment>(N.Entity);
		if (!Foot)
		{
			continue;
		}
		for (int32 y = 0; y < Foot->SizeY; ++y)
		{
			for (int32 x = 0; x < Foot->SizeX; ++x)
			{
				Occupancy.Add(FIntPoint(Foot->OriginGridLocation.X + x, Foot->OriginGridLocation.Y + y), NodeId);
			}
		}

		const UAsceEtherBuildingData* Data = FindBuildingData(N.BuildingDefId);
		if (!Data)
		{
			continue; // 注册时已告警
		}

		// 配置校验（只告警、不跳过：跳过会打乱端口下标与 InternalLinks/角色表的对应关系）
		for (int32 pi = 0; pi < Data->Ports.Num(); ++pi)
		{
			const FAsceEtherPortDef& DefI = Data->Ports[pi];
			if (DefI.Dim >= Data->SupportedDimCount)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Ether] 端口维度超出 SupportedDimCount：%s 端口 %d（Dim=%d, Supported=%d）"),
					*N.BuildingDefId.ToString(), pi, DefI.Dim, Data->SupportedDimCount);
			}
			for (int32 pj = 0; pj < pi; ++pj)
			{
				const FAsceEtherPortDef& DefJ = Data->Ports[pj];
				if (DefI.CellOffsetX == DefJ.CellOffsetX && DefI.CellOffsetY == DefJ.CellOffsetY
					&& DefI.Dir == DefJ.Dir && DefI.Dim == DefJ.Dim)
				{
					UE_LOG(LogTemp, Warning, TEXT("[Ether] 重复端口（同格/同向/同维）：%s 端口 %d 与 %d"),
						*N.BuildingDefId.ToString(), pj, pi);
				}
			}
		}

		// 器件 = 时序（由 DeviceOp 决定行为）；传输器 = 纯组合
		N.bDevice = Data->bIsDevice;
		N.DeviceOp = static_cast<uint8>(Data->DeviceOp);
		N.DeviceParamSignal = Data->DeviceParamSignal;
		N.DeviceParamA = Data->DeviceParamA;
		N.RecipeInputs = Data->Inputs;
		N.RecipeOutput = Data->Output;

		TArray<int32>& PortIds = NodePortIds[NodeId];
		N.PortStart = Ports.Num();
		N.PortCount = Data->Ports.Num();
		PortIds.Reserve(N.PortCount);
		for (int32 pi = 0; pi < N.PortCount; ++pi)
		{
			const FAsceEtherPortDef& Def = Data->Ports[pi];
			FPort P;
			P.NodeId = NodeId;
			P.Dir = Def.Dir;
			P.Dim = Def.Dim;
			P.CellX = Foot->OriginGridLocation.X + Def.CellOffsetX;
			P.CellY = Foot->OriginGridLocation.Y + Def.CellOffsetY;
			P.Throughput = FMath::Max<uint16>(1, Def.Type ? Def.Type->ThroughputPerTick : 1);
			P.bIsInput = false; // 入/出不由端口自己决定：器件看角色表、传输器看布线
			P.Op = Def.Op;
			P.SplitMode = Def.SplitMode;
			PortIds.Add(Ports.Add(P));
		}

		if (N.bDevice)
		{
			// 器件：入/出由角色表决定
			for (const int32 Idx : Data->InputPorts)
			{
				if (!PortIds.IsValidIndex(Idx))
				{
					UE_LOG(LogTemp, Warning, TEXT("[Ether] InputPorts 端口下标越界：%s (%d)"), *N.BuildingDefId.ToString(), Idx);
					continue;
				}
				const int32 Pid = PortIds[Idx];
				Ports[Pid].bIsInput = true;
				N.DeviceInputPorts.Add(Pid);
			}
			for (const int32 Idx : Data->OutputPorts)
			{
				if (!PortIds.IsValidIndex(Idx))
				{
					UE_LOG(LogTemp, Warning, TEXT("[Ether] OutputPorts 端口下标越界：%s (%d)"), *N.BuildingDefId.ToString(), Idx);
					continue;
				}
				N.DeviceOutputPorts.Add(PortIds[Idx]);
			}
		}
		// 传输器：先全部当"出"，入由内部布线在 ② 里回填（被指向的端口 = 入）。

		// Register 器件：输入端口应为 8 位 + 1 enable（超出部分会被忽略）
		if (N.bDevice && N.DeviceOp == static_cast<uint8>(EAsceEtherDeviceOp::Register) && N.DeviceInputPorts.Num() > 9)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Ether] Register 输入端口超过 9 个（8 位 + enable；多余会被忽略）：%s"),
				*N.BuildingDefId.ToString());
		}

		// 恢复旧运行态（同建筑类型端口布局不变 → (nodeId, 本地端口下标) 映射稳定）
		for (int32 pi = 0; pi < N.PortCount; ++pi)
		{
			const TPair<uint16, int16>* Saved = SavedPortState.Find(
				(static_cast<uint64>(static_cast<uint32>(NodeId)) << 32) | static_cast<uint32>(pi));
			if (Saved)
			{
				FPort& P = Ports[N.PortStart + pi];
				P.Ether = Saved->Key;
				P.SignalType = Saved->Value;
			}
		}
	}

	// ② 内部布线（变长边表；传输器的入/出由此推导）
	for (int32 NodeId = 0; NodeId < NodeNum; ++NodeId)
	{
		if (!IsNodeAlive(NodeId) || NodePortIds[NodeId].Num() == 0)
		{
			continue;
		}
		const UAsceEtherBuildingData* Data = FindBuildingData(Nodes[NodeId].BuildingDefId);
		if (!Data)
		{
			continue;
		}
		const TArray<int32>& PortIds = NodePortIds[NodeId];
		for (const FAsceEtherLinkDef& LD : Data->InternalLinks)
		{
			if (LD.FromPort == LD.ToPort)
			{
				continue; // 不自连
			}
			if (!PortIds.IsValidIndex(LD.FromPort) || !PortIds.IsValidIndex(LD.ToPort))
			{
				UE_LOG(LogTemp, Warning, TEXT("[Ether] InternalLinks 端口下标越界：%s (%d -> %d)"),
					*Nodes[NodeId].BuildingDefId.ToString(), LD.FromPort, LD.ToPort);
				continue;
			}
			const int32 FromPort = PortIds[LD.FromPort];
			const int32 ToPort = PortIds[LD.ToPort];
			if (!Ports[FromPort].LinkTargets.Contains(ToPort))
			{
				Ports[FromPort].LinkTargets.Add(ToPort);
				Links.Add(FAsceEtherLink{ FromPort, ToPort });
			}
			if (!Nodes[NodeId].bDevice)
			{
				// 传输器：内部布线的**源**端口 = 外部的入端口（它接收外部、向内转发；目标端口才是对外输出）
				Ports[FromPort].bIsInput = true;
			}
		}
	}

	// 分流/背压顺序（§2.9）：相对源端口，从它**左手边**起顺时针；同方向内 Dim 低→高
	for (int32 PortId = 0; PortId < Ports.Num(); ++PortId)
	{
		FPort& P = Ports[PortId];
		const int32 SrcDir = static_cast<int32>(P.Dir);
		P.LinkTargets.Sort([this, SrcDir](int32 A, int32 B)
		{
			const FPort& PA = Ports[A];
			const FPort& PB = Ports[B];
			// rank = (Dir - SrcDir + 1) & 3；rank 0 = 源端口左手边那个方向
			const int32 RA = (static_cast<int32>(PA.Dir) - SrcDir + 1) & 3;
			const int32 RB = (static_cast<int32>(PB.Dir) - SrcDir + 1) & 3;
			if (RA != RB)
			{
				return RA < RB;
			}
			if (PA.Dim != PB.Dim)
			{
				return PA.Dim < PB.Dim;
			}
			// 同 (rank, Dim) 的多格端口：用格坐标/PortId 兜底，保证顺序确定（Sort 本身不稳定）
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

	// ③ 外部连接：出端口沿方向正对找第一个占用建筑（挡住不穿透；1:1）
	for (int32 PortId = 0; PortId < Ports.Num(); ++PortId)
	{
		FPort& SPort = Ports[PortId];
		if (SPort.bIsInput)
		{
			continue; // 入端口不射出
		}

		int32 dx = 0, dy = 0;
		switch (static_cast<AsceEther::EDir>(SPort.Dir))
		{
			case AsceEther::EDir::Left:  dx = -1; break;
			case AsceEther::EDir::Right: dx = 1; break;
			case AsceEther::EDir::Up:    dy = 1; break;
			case AsceEther::EDir::Down:  dy = -1; break;
			default: break;
		}

		const uint8 OppDir = static_cast<uint8>(AsceEther::Opposite(static_cast<AsceEther::EDir>(SPort.Dir)));
		int32 cx = SPort.CellX;
		int32 cy = SPort.CellY;
		int32 ToPort = INDEX_NONE;
		for (int32 step = 0; step < MaxScanCells; ++step)
		{
			cx += dx;
			cy += dy;
			const int32* Found = Occupancy.Find(FIntPoint(cx, cy));
			if (!Found)
			{
				continue; // 空地：继续
			}
			const int32 B = *Found;
			if (B == SPort.NodeId)
			{
				break; // 打到自己：停
			}
			const int32 BStart = Nodes[B].PortStart;
			const int32 BCount = Nodes[B].PortCount;
			for (int32 qi = 0; qi < BCount; ++qi)
			{
				const FPort& Q = Ports[BStart + qi];
				// 必须落在射线命中的那一格（多格建筑在 (方向,维度) 上可能有多个端口）
				if (Q.bIsInput && Q.Dir == OppDir && Q.Dim == SPort.Dim && Q.CellX == cx && Q.CellY == cy)
				{
					ToPort = BStart + qi;
					break;
				}
			}
			break; // 遇到第一个建筑就停（不管接不接）
		}

		if (ToPort != INDEX_NONE)
		{
			SPort.OutEdge = Edges.Add(FAsceEtherEdge{ PortId, ToPort });
		}
	}

	// ③.5 入边源端口表（内部布线的源 + 外部入边的源）+ 本拍符号记录数组（And/Or 求值用）
	for (const FAsceEtherLink& L : Links)
	{
		if (Ports.IsValidIndex(L.ToPort))
		{
			Ports[L.ToPort].InSources.AddUnique(L.FromPort);
		}
	}
	for (const FAsceEtherEdge& E : Edges)
	{
		if (Ports.IsValidIndex(E.ToPort))
		{
			Ports[E.ToPort].InSources.AddUnique(E.FromPort);
		}
	}
	for (FPort& P : Ports)
	{
		P.InSigns.Init(0, P.InSources.Num());
		P.InSignTick.Init(0, P.InSources.Num());
	}

	// ── ④ 组合段（端口级）：组合端口 = 非器件端口；段内 Kahn 拓扑序填 ProcessOrder ──

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
	for (const FAsceEtherLink& L : Links)
	{
		if (IsCombPort(L.FromPort) && IsCombPort(L.ToPort))
		{
			CombOut[L.FromPort].Add(L.ToPort);
			CombUndir[L.FromPort].Add(L.ToPort);
			CombUndir[L.ToPort].Add(L.FromPort);
		}
	}
	for (const FAsceEtherEdge& E : Edges)
	{
		if (IsCombPort(E.FromPort) && IsCombPort(E.ToPort))
		{
			CombOut[E.FromPort].Add(E.ToPort);
			CombUndir[E.FromPort].Add(E.ToPort);
			CombUndir[E.ToPort].Add(E.FromPort);
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
			// 组合环（无器件破环）→ 非法：只把已排出拓扑序的端口接入处理，
			// 环上的端口不参与运算（避免"带着环跑"产生不确定结果）。开发期应避免这种布线。
			UE_LOG(LogTemp, Error, TEXT("[Ether] 组合环：段内 %d 个端口只排出 %d 个（环上端口已拒绝处理，应由器件破环）"),
				Comp.Num(), Order.Num());
		}

		FAsceEtherSegment Seg;
		Seg.Start = ProcessOrder.Num();
		Seg.Count = Order.Num();
		const int32 SegId = Segments.Add(Seg);
		for (const int32 U : Order)
		{
			ProcessOrder.Add(U);
			PortSegId[U] = SegId;
		}
	}

	// ── ⑤ 器件 node -> 相邻段（它输出的段 + 喂它的段） ──

	auto LinkDeviceSeg = [&](int32 PortA, int32 PortB)
	{
		const bool bAComb = IsCombPort(PortA);
		const bool bBComb = IsCombPort(PortB);
		if (bAComb == bBComb)
		{
			return; // 都是组合（已归段）或都是器件端口
		}
		const int32 CombPort = bAComb ? PortA : PortB;
		const int32 DevNode = Ports[bAComb ? PortB : PortA].NodeId;
		const int32 Seg = PortSegId[CombPort];
		if (Seg != INDEX_NONE && DeviceAdjSegs.IsValidIndex(DevNode))
		{
			DeviceAdjSegs[DevNode].AddUnique(Seg);
		}
	};
	for (const FAsceEtherLink& L : Links)
	{
		LinkDeviceSeg(L.FromPort, L.ToPort);
	}
	for (const FAsceEtherEdge& E : Edges)
	{
		LinkDeviceSeg(E.FromPort, E.ToPort);
	}

	// 初始活跃段：结构变化/首帧后，把仍有残留的段直接加入活跃表（不依赖器件采样先注入）。
	// 注意：直接写 Stamp/ActiveList，不能用 ActivateSeg——ClearGraph 刚把 CurrentStamp 复位为 0，
	// 与段的初始 Stamp == 0 相等会被判成"本帧已加入"而跳过。
	for (int32 SegId = 0; SegId < Segments.Num(); ++SegId)
	{
		const FAsceEtherSegment& Seg = Segments[SegId];
		bool bHasEther = false;
		for (int32 k = Seg.Start; k < Seg.Start + Seg.Count && !bHasEther; ++k)
		{
			bHasEther = Ports[ProcessOrder[k]].Ether > 0;
		}
		if (bHasEther)
		{
			Segments[SegId].Stamp = CurrentStamp;
			ActiveList.Add(SegId);
		}
	}
}

// ── 每 tick ──

void UAsceEtherNetworkSubsystem::RunEtherTick()
{
	// 结构变化（注册/注销建筑）延迟到这里统一重建：批量放置只重建一次
	if (bGraphDirty)
	{
		RebuildGraph();
	}

	++CurrentStamp;

	const int32 NodeNum = Nodes.Num();
	if (Ports.Num() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	UMassEntitySubsystem* MassSub = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	const FMassEntityManager* EM = MassSub ? &MassSub->GetEntityManager() : nullptr;
	if (!EM)
	{
		return;
	}

	// ── ① 器件采样（时序：拍边界；行为由 DeviceOp 决定） ──
	// 两遍处理，保证"器件 → 器件"的外部边也是 1 tick/级（与 nodeId 顺序无关）：
	//   Pass 1：全体器件采样（读输入 → 更新内部状态 → 写自己的输出端口缓冲）+ 激活相邻段；
	//   Pass 2：全体器件统一把输出端口推出去（含上拍残留），推不掉留着下拍继续。
	// 这样本拍任何器件写入下游器件入端口的料，下游只能下一拍采样到；写进组合段的料在本拍 ② 里继续传播。
	TArray<int32> NodeOutPortUsed;
	NodeOutPortUsed.Init(INDEX_NONE, NodeNum);

	for (int32 NodeId = 0; NodeId < NodeNum; ++NodeId)
	{
		if (!IsDevice(NodeId))
		{
			continue;
		}
		FNode& N = Nodes[NodeId];
		const int32 PCount = N.PortCount;
		if (PCount <= 0)
		{
			continue;
		}
		const EAsceEtherDeviceOp DeviceOp = static_cast<EAsceEtherDeviceOp>(N.DeviceOp);
		FEtherDeviceStateFragment* State = EM->GetFragmentDataPtr<FEtherDeviceStateFragment>(N.Entity);

		bool bChanged = false;
		int32 OutPortUsed = INDEX_NONE;   // 本拍写出/需要继续推出的输出端口

		if (DeviceOp == EAsceEtherDeviceOp::Facility)
		{
			// 输出端口 = 角色表第一个（没配角色表则退化为"第一个非入端口"）
			int32 OutPort = (N.DeviceOutputPorts.Num() > 0) ? N.DeviceOutputPorts[0] : INDEX_NONE;
			if (OutPort == INDEX_NONE)
			{
				for (int32 pi = 0; pi < PCount; ++pi)
				{
					const int32 Pid = N.PortStart + pi;
					if (!Ports[Pid].bIsInput)
					{
						OutPort = Pid;
						break;
					}
				}
			}
			OutPortUsed = OutPort;
			if (OutPort != INDEX_NONE && N.RecipeOutput.AmountPerTick > 0)
			{
				FPort& OP = Ports[OutPort];
				const uint16 OutRem = (OP.Throughput > OP.Ether)
					? static_cast<uint16>(OP.Throughput - OP.Ether) : 0;

				// 配方输入是否都够（只看角色表的入端口）。
				// 空配方（无 Inputs）= 根产源（矿机/发电机型）：不扣料、自由产出，仅受输出端口吞吐限制。
				bool bEnough = true;
				for (int32 ii = 0; ii < N.RecipeInputs.Num() && bEnough; ++ii)
				{
					const FAsceEtherRecipeItem& Item = N.RecipeInputs[ii];
					uint32 Avail = 0;
					for (const int32 Pid : N.DeviceInputPorts)
					{
						const FPort& IP = Ports[Pid];
						if (IP.Ether > 0 && IP.SignalType == Item.SignalType)
						{
							Avail += IP.Ether;
						}
					}
					if (Avail < Item.AmountPerTick)
					{
						bEnough = false;
					}
				}

				const bool bOutTypeOk = (OP.Ether == 0) || (OP.SignalType == N.RecipeOutput.SignalType);
				if (bEnough && bOutTypeOk && OutRem >= N.RecipeOutput.AmountPerTick)
				{
					// 扣输入
					for (int32 ii = 0; ii < N.RecipeInputs.Num(); ++ii)
					{
						const FAsceEtherRecipeItem& Item = N.RecipeInputs[ii];
						uint32 Need = Item.AmountPerTick;
						for (const int32 Pid : N.DeviceInputPorts)
						{
							if (Need == 0)
							{
								break;
							}
							FPort& IP = Ports[Pid];
							if (IP.Ether == 0 || IP.SignalType != Item.SignalType)
							{
								continue;
							}
							const uint16 Take = static_cast<uint16>(FMath::Min<uint32>(IP.Ether, Need));
							IP.Ether = static_cast<uint16>(IP.Ether - Take);
							Need -= Take;
							if (IP.Ether == 0)
							{
								IP.SignalType = 0;
							}
						}
					}
					if (OP.Ether == 0)
					{
						OP.SignalType = N.RecipeOutput.SignalType;
					}
					OP.Ether = static_cast<uint16>(OP.Ether + N.RecipeOutput.AmountPerTick);
					bChanged = true;
				}
			}
		}
		else if (DeviceOp == EAsceEtherDeviceOp::Sink)
		{
			// 汇：吃掉角色表入端口的 ether
			for (const int32 Pid : N.DeviceInputPorts)
			{
				FPort& IP = Ports[Pid];
				if (IP.Ether > 0)
				{
					IP.Ether = 0;
					IP.SignalType = 0;
					bChanged = true;
				}
			}
		}
		else if (DeviceOp == EAsceEtherDeviceOp::Latch && State)
		{
			// 记忆单元：① 写入 = 入端口上出现"非 0 号"的料 ⇒ 消耗掉它、覆盖编号（信息，不守恒）
			for (const int32 Pid : N.DeviceInputPorts)
			{
				FPort& IP = Ports[Pid];
				if (IP.Ether > 0 && AsceEther::Family(IP.SignalType) != 0)
				{
					State->StoredType = IP.SignalType;
					State->bHasStored = true;
					IP.Ether = 0;          // 写入把送来的料消耗掉
					IP.SignalType = 0;
					bChanged = true;
				}
			}

			// ② 读取 = 用 0 号 ether（能量）重铸编号 ⇒ 产出；`1 份 = abs(编号)` 份能量；能量不足 ⇒ 不产出（不消耗）
			int32 OutPort = (N.DeviceOutputPorts.Num() > 0) ? N.DeviceOutputPorts[0] : INDEX_NONE;
			if (OutPort == INDEX_NONE)
			{
				for (int32 pi = 0; pi < PCount; ++pi)
				{
					const int32 Pid = N.PortStart + pi;
					if (!Ports[Pid].bIsInput)
					{
						OutPort = Pid;
						break;
					}
				}
			}
			OutPortUsed = OutPort;
			if (State->bHasStored && OutPort != INDEX_NONE)
			{
				// 能量不足：本拍不产出（提示"需要更快的能量线"留给 UI 层）
				if (MintFromEnergy(N, OutPort, State->StoredType))
				{
					bChanged = true;
				}
			}
		}
		else if (DeviceOp == EAsceEtherDeviceOp::Clock && State)
		{
			// 时钟：自翻转编号（半周期 = DeviceParamA tick），每拍用能量重铸当前编号（= 自驱动的记忆单元）
			if (!State->bHasStored)
			{
				State->StoredType = (N.DeviceParamSignal != 0) ? N.DeviceParamSignal : AsceEther::GenericSignalType;
				State->bHasStored = true;
				State->Progress = 0;
			}
			const uint16 HalfPeriod = FMath::Max<uint16>(1, N.DeviceParamA);
			if (++State->Progress >= HalfPeriod)
			{
				State->StoredType = AsceEther::Complement(State->StoredType);
				State->Progress = 0;
				bChanged = true;
			}

			int32 OutPort = (N.DeviceOutputPorts.Num() > 0) ? N.DeviceOutputPorts[0] : INDEX_NONE;
			OutPortUsed = OutPort;
			if (OutPort != INDEX_NONE && MintFromEnergy(N, OutPort, State->StoredType))
			{
				bChanged = true;
			}
		}
		else if (DeviceOp == EAsceEtherDeviceOp::Counter && State)
		{
			// 计数器：入端口每来 1 份"非 0 号"的料算 1 个脉冲（0 号 = 能量，不计数）
			for (const int32 Pid : N.DeviceInputPorts)
			{
				FPort& IP = Ports[Pid];
				if (IP.Ether > 0 && AsceEther::Family(IP.SignalType) != 0)
				{
					State->Progress = static_cast<uint16>(FMath::Min<uint32>(0xFFFF, State->Progress + IP.Ether));
					IP.Ether = 0;
					IP.SignalType = 0;
					bChanged = true;
				}
			}

			// 每累计 `DeviceParamA` 份脉冲 ⇒ 产 1 份编号（用能量重铸）
			const uint16 Threshold = FMath::Max<uint16>(1, N.DeviceParamA);
			int32 OutPort = (N.DeviceOutputPorts.Num() > 0) ? N.DeviceOutputPorts[0] : INDEX_NONE;
			OutPortUsed = OutPort;
			if (State->Progress >= Threshold && OutPort != INDEX_NONE)
			{
				const int16 OutType = (N.DeviceParamSignal != 0) ? N.DeviceParamSignal : AsceEther::GenericSignalType;
				if (MintFromEnergy(N, OutPort, OutType))
				{
					State->Progress = static_cast<uint16>(State->Progress - Threshold);
					bChanged = true;
				}
			}
		}
		else if (DeviceOp == EAsceEtherDeviceOp::Annihilator)
		{
			// 湮灭发电机：一组同族异号（+X 与 −X）⇒ |X| 份 0 号 ether（能量）
			int32 OutPort = (N.DeviceOutputPorts.Num() > 0) ? N.DeviceOutputPorts[0] : INDEX_NONE;
			if (OutPort != INDEX_NONE)
			{
				int32 PlusPort = INDEX_NONE;
				int32 MinusPort = INDEX_NONE;
				for (const int32 Pid : N.DeviceInputPorts)
				{
					const FPort& IP = Ports[Pid];
					if (IP.Ether == 0 || AsceEther::Family(IP.SignalType) == 0)
					{
						continue;
					}
					if (AsceEther::IsComplement(IP.SignalType))
					{
						if (MinusPort == INDEX_NONE)
						{
							MinusPort = Pid;
						}
					}
					else if (PlusPort == INDEX_NONE)
					{
						PlusPort = Pid;
					}
				}
				if (PlusPort != INDEX_NONE && MinusPort != INDEX_NONE
					&& AsceEther::IsSameFamily(Ports[PlusPort].SignalType, Ports[MinusPort].SignalType))
				{
					const uint16 FamilyId = AsceEther::Family(Ports[PlusPort].SignalType); // = |X|（核数）
					FPort& OP = Ports[OutPort];
					const uint16 OutRem = (OP.Throughput > OP.Ether)
						? static_cast<uint16>(OP.Throughput - OP.Ether) : 0;
					const bool bOutTypeOk = (OP.Ether == 0) || (OP.SignalType == 0);
					uint32 Pairs = FMath::Min<uint16>(Ports[PlusPort].Ether, Ports[MinusPort].Ether);
					Pairs = FMath::Min<uint32>(Pairs, OutRem / FamilyId);
					if (bOutTypeOk && Pairs > 0)
					{
						FPort& SP = Ports[PlusPort];
						FPort& SM = Ports[MinusPort];
						SP.Ether = static_cast<uint16>(SP.Ether - Pairs);
						SM.Ether = static_cast<uint16>(SM.Ether - Pairs);
						if (SP.Ether == 0)
						{
							SP.SignalType = 0;
						}
						if (SM.Ether == 0)
						{
							SM.SignalType = 0;
						}
						if (OP.Ether == 0)
						{
							OP.SignalType = 0;
						}
						OP.Ether = static_cast<uint16>(OP.Ether + Pairs * FamilyId);
						bChanged = true;
					}
				}
			}
		}
		else if (DeviceOp == EAsceEtherDeviceOp::Register && State)
		{
			// 寄存器：角色表 `InputPorts = [bit0 .. bitN-1, enable]`、`OutputPorts = [bit0 .. bitN-1]`
			// 位宽上限 8（= 单格的维度数）；多位由玩家拼多个寄存器
			const int32 BitCount = FMath::Clamp<int32>(N.DeviceInputPorts.Num() - 1, 0, 8);
			const int32 EnablePort = (N.DeviceInputPorts.Num() > 0) ? N.DeviceInputPorts.Last() : INDEX_NONE;

			bool bWrite = false;
			if (EnablePort != INDEX_NONE)
			{
				const FPort& EP = Ports[EnablePort];
				bWrite = (EP.Ether > 0) && (AsceEther::Family(EP.SignalType) != 0) && !AsceEther::IsComplement(EP.SignalType);
			}

			if (bWrite)
			{
				// 并行加载：有料 ⇒ 覆盖该位（消耗那份料）；没料 ⇒ 清空该位
				for (int32 i = 0; i < BitCount; ++i)
				{
					FPort& DP = Ports[N.DeviceInputPorts[i]];
					if (DP.Ether > 0 && AsceEther::Family(DP.SignalType) != 0)
					{
						State->StoredBits[i] = DP.SignalType;
						bChanged = true;
					}
					else
					{
						State->StoredBits[i] = 0;
					}
					if (DP.Ether > 0)
					{
						DP.Ether = 0;
						DP.SignalType = 0;
					}
				}
				State->bHasStored = true;
				FPort& EP = Ports[EnablePort];
				EP.Ether = 0;
				EP.SignalType = 0;
				bChanged = true;
			}

			// 读取：每拍把有值的位用能量重铸到对应输出端口
			if (State->bHasStored)
			{
				const int32 OutNum = FMath::Min(BitCount, N.DeviceOutputPorts.Num());
				for (int32 i = 0; i < OutNum; ++i)
				{
					if (State->StoredBits[i] != 0 && MintFromEnergy(N, N.DeviceOutputPorts[i], State->StoredBits[i]))
					{
						bChanged = true;
					}
				}
			}
		}
		// TODO：更多器件 op（如移位寄存器、比较器等）

		if (bChanged)
		{
			for (const int32 Seg : DeviceAdjSegs[NodeId])
			{
				ActivateSeg(Seg);
			}
		}

		// 记录本拍写出/需要推出的输出端口，Pass 2 统一推送
		NodeOutPortUsed[NodeId] = OutPortUsed;
	}

	// Pass 2：全体器件统一推送输出端口（可能还有上拍残留；推不掉就留着，下拍继续）
	for (int32 NodeId = 0; NodeId < NodeNum; ++NodeId)
	{
		if (!IsDevice(NodeId))
		{
			continue;
		}
		FNode& N = Nodes[NodeId];
		bool bOutLeft = false;
		for (const int32 Pid : N.DeviceOutputPorts)
		{
			PushPortOutput(Pid);
			if (Ports[Pid].Ether > 0)
			{
				bOutLeft = true;
			}
		}
		const int32 OutPortUsed = NodeOutPortUsed[NodeId];
		if (OutPortUsed != INDEX_NONE && !N.DeviceOutputPorts.Contains(OutPortUsed))
		{
			PushPortOutput(OutPortUsed);
			if (Ports[OutPortUsed].Ether > 0)
			{
				bOutLeft = true;
			}
		}
		if (bOutLeft)
		{
			for (const int32 Seg : DeviceAdjSegs[NodeId])
			{
				ActivateSeg(Seg);
			}
		}
	}

	// ── ② 处理活跃段（段内拓扑序：内部布线分流 → 外部推送；判空决定去留） ──
	for (int32 Idx = 0; Idx < ActiveList.Num(); )
	{
		const int32 SegId = ActiveList[Idx];
		const FAsceEtherSegment& Seg = Segments[SegId];
		bool bStillHasEther = false;
		for (int32 k = Seg.Start; k < Seg.Start + Seg.Count; ++k)
		{
			ProcessPort(ProcessOrder[k], bStillHasEther);
		}

		if (bStillHasEther)
		{
			// 留到下一帧：stamp 设成下一帧，ActivateSeg 会跳过（避免重复入列）
			Segments[SegId].Stamp = CurrentStamp + 1;
			++Idx;
		}
		else
		{
			ActiveList.RemoveAtSwap(Idx, 1, EAllowShrinking::No);
		}
	}
}

void UAsceEtherNetworkSubsystem::ProcessPort(int32 PortId, bool& bOutHasEther)
{
	EvaluateOp(PortId);
	PushPortOutput(PortId);

	// 判空：本端口还有料 → 本段保持活跃
	if (Ports[PortId].Ether > 0)
	{
		bOutHasEther = true;
	}
}

void UAsceEtherNetworkSubsystem::EvaluateOp(int32 PortId)
{
	FPort& P = Ports[PortId];
	if (P.Op != EAsceEtherPortOp::And && P.Op != EAsceEtherPortOp::Or)
	{
		return; // Wire / Not 不做多输入求值
	}

	// 只看"本拍真的送来了料"的入边（Empty = 未定值，不计入）
	bool bHas = false;
	bool bAnyPlus = false;
	bool bAnyMinus = false;
	int16 BasePos = 0;
	for (int32 i = 0; i < P.InSources.Num(); ++i)
	{
		if (!P.InSignTick.IsValidIndex(i) || P.InSignTick[i] != CurrentStamp)
		{
			continue;
		}
		const int16 T = P.InSigns[i];
		if (!bHas)
		{
			bHas = true;
			BasePos = AsceEther::IsComplement(T) ? AsceEther::Complement(T) : T;
		}
		else if (!AsceEther::IsSameFamily(BasePos, T))
		{
			return; // 异族（合流本应挡住）→ 本拍不动作 = 堵死
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
	if (!bHas || P.Ether == 0)
	{
		return; // 无输入 ⇒ 不输出（Empty ≠ 逻辑 0）
	}

	int16 ResType;
	if (AsceEther::Family(BasePos) == 0)
	{
		ResType = 0; // 0 号（能量）逻辑透明：原样透传（Not 后仍是 0）
	}
	else
	{
		const bool bPlus = (P.Op == EAsceEtherPortOp::And) ? !bAnyMinus : bAnyPlus;
		ResType = bPlus ? BasePos : AsceEther::Complement(BasePos);
	}
	P.SignalType = ResType; // 份数不变，只统一符号（§2.7）
}

void UAsceEtherNetworkSubsystem::PushPortOutput(int32 PortId)
{
	FPort& P = Ports[PortId];
	if (P.Ether == 0)
	{
		return;
	}

	// Not = 推出时取互补态；And/Or 的符号已在 EvaluateOp 里定好
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
				const FPort& Q = Ports[ToPort];
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
				const uint16 Share = static_cast<uint16>((P.Ether + Left - 1) / Left); // ceil(剩余/剩余目标数)
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
				MoveUnits(PortId, ToPort, P.Ether, OutType);
			}
		}
	}

	// ② 外部推送（1:1）
	if (P.Ether > 0 && P.OutEdge != INDEX_NONE)
	{
		MoveUnits(PortId, Edges[P.OutEdge].ToPort, P.Ether, OutType);
	}
}

uint16 UAsceEtherNetworkSubsystem::MoveUnits(int32 FromPort, int32 ToPort, uint16 Amount, int16 Type)
{
	if (Amount == 0 || !Ports.IsValidIndex(FromPort) || !Ports.IsValidIndex(ToPort))
	{
		return 0;
	}
	FPort& S = Ports[FromPort];
	FPort& T = Ports[ToPort];
	const uint16 Before = S.Ether;
	uint16 Left = Amount;

	// 目标已有料：先判族（异族 → 堵死：不消费也不输出）
	if (T.Ether > 0)
	{
		if (!AsceEther::IsSameFamily(T.SignalType, Type))
		{
			return 0;
		}
		// `And/Or` 目标不湮灭：它要"看到"各入边送来的符号；份数照收（§2.7 份数不吞不吐）
		const bool bLogicTarget = (T.Op == EAsceEtherPortOp::And) || (T.Op == EAsceEtherPortOp::Or);
		if (!bLogicTarget && AsceEther::IsComplement(T.SignalType) != AsceEther::IsComplement(Type))
		{
			if (T.InSources.Num() > 1)
			{
				// 合流节点（结构上多条入边）：不同来源往同一节点送入相反的 ether → 同族异号 1:1 湮灭
				const uint16 Kill = FMath::Min<uint16>(Left, T.Ether);
				S.Ether = static_cast<uint16>(S.Ether - Kill);
				T.Ether = static_cast<uint16>(T.Ether - Kill);
				Left = static_cast<uint16>(Left - Kill);
			}
			else
			{
				// 单一来源的普通节点：符号直接翻转（清空该端口残留、采用来料；值变化即时响应，§2.7）
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

	// `And/Or` 目标：记录本拍这条入边送来的符号（EvaluateOp 用）
	if (Processed > 0 && ((T.Op == EAsceEtherPortOp::And) || (T.Op == EAsceEtherPortOp::Or)))
	{
		const int32 Idx = T.InSources.IndexOfByKey(FromPort);
		if (T.InSigns.IsValidIndex(Idx) && T.InSignTick.IsValidIndex(Idx))
		{
			T.InSigns[Idx] = Type;
			T.InSignTick[Idx] = CurrentStamp;
		}
	}
	return Processed;
}

bool UAsceEtherNetworkSubsystem::MintFromEnergy(const FNode& Node, int32 OutPort, int16 Type)
{
	if (!Ports.IsValidIndex(OutPort))
	{
		return false;
	}
	// 0 号（能量）不能靠能量重铸（否则能量就能凭空翻倍）
	const uint16 Cost = AsceEther::Family(Type);
	if (Cost == 0)
	{
		return false;
	}

	uint32 EnergyAvail = 0;
	for (const int32 Pid : Node.DeviceInputPorts)
	{
		const FPort& IP = Ports[Pid];
		if (IP.Ether > 0 && AsceEther::Family(IP.SignalType) == 0)
		{
			EnergyAvail += IP.Ether;
		}
	}

	FPort& OP = Ports[OutPort];
	const uint16 OutRem = (OP.Throughput > OP.Ether)
		? static_cast<uint16>(OP.Throughput - OP.Ether) : 0;
	const bool bOutTypeOk = (OP.Ether == 0) || (OP.SignalType == Type);
	const uint32 Produce = FMath::Min<uint32>(EnergyAvail / Cost, OutRem);
	if (!bOutTypeOk || Produce == 0)
	{
		return false; // 能量不足（不产出、也不消耗）或端口被占/满了
	}

	uint32 NeedEnergy = Produce * Cost;
	for (const int32 Pid : Node.DeviceInputPorts)
	{
		if (NeedEnergy == 0)
		{
			break;
		}
		FPort& IP = Ports[Pid];
		if (IP.Ether > 0 && AsceEther::Family(IP.SignalType) == 0)
		{
			const uint16 Take = static_cast<uint16>(FMath::Min<uint32>(IP.Ether, NeedEnergy));
			IP.Ether = static_cast<uint16>(IP.Ether - Take);
			NeedEnergy -= Take;
			if (IP.Ether == 0)
			{
				IP.SignalType = 0;
			}
		}
	}

	if (OP.Ether == 0)
	{
		OP.SignalType = Type;
	}
	OP.Ether = static_cast<uint16>(OP.Ether + Produce);
	return true;
}
