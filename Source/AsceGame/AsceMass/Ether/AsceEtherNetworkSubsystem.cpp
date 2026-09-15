// AsceEtherNetworkSubsystem.cpp
#include "AsceEtherNetworkSubsystem.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "AsceMass/Fragments/AsceFootprintFragment.h"

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

	// 结构变化：重建（先整图重建；后续可做脏区域增量）
	RebuildGraph();
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

	RebuildGraph();
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

		// 器件 = 时序（由 DeviceOp 决定行为）；传输器 = 纯组合
		N.bDevice = Data->bIsDevice;
		N.DeviceOp = static_cast<uint8>(Data->DeviceOp);
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
				Ports[ToPort].bIsInput = true; // 传输器：被指向的端口 = 入
			}
		}
	}

	// 分流顺序：先按 (Dir, Dim) 排（TODO 步骤③：改成"相对源端口左手边起顺时针"的扫描序 + SplitMode）
	for (FPort& P : Ports)
	{
		P.LinkTargets.Sort([this](int32 A, int32 B)
		{
			const FPort& PA = Ports[A];
			const FPort& PB = Ports[B];
			return (PA.Dir != PB.Dir) ? (PA.Dir < PB.Dir) : (PA.Dim < PB.Dim);
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
		for (int32 step = 0; step < AsceEther::MaxLinkRangeCells; ++step)
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
				if (Q.bIsInput && Q.Dir == OppDir && Q.Dim == SPort.Dim)
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
			// 组合环（无器件破环）→ 非法；这里兜底把剩余端口接上（开发期应避免）
			UE_LOG(LogTemp, Error, TEXT("[Ether] 组合环：段内 %d 个端口只排出 %d 个（应有器件破环）"), Comp.Num(), Order.Num());
			for (const int32 U : Comp)
			{
				Order.AddUnique(U);
			}
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

	// TODO(阶段⑥)：首帧活跃表（可把有残留的段加入）；当前留空，靠器件采样注入。
}

// ── 每 tick ──

void UAsceEtherNetworkSubsystem::RunEtherTick()
{
	++CurrentStamp;

	const int32 NodeNum = Nodes.Num();
	if (Ports.Num() == 0)
	{
		return;
	}

	// ── ① 器件采样（时序：拍边界；行为由 DeviceOp 决定） ──
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

		bool bChanged = false;

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
			if (OutPort != INDEX_NONE && N.RecipeOutput.AmountPerTick > 0)
			{
				FPort& OP = Ports[OutPort];
				const uint16 OutRem = (OP.Throughput > OP.Ether)
					? static_cast<uint16>(OP.Throughput - OP.Ether) : 0;

				// 配方输入是否都够（只看角色表的入端口）
				bool bEnough = (N.RecipeInputs.Num() > 0);
				for (int32 ii = 0; ii < N.RecipeInputs.Num() && bEnough; ++ii)
				{
					const FAsceEtherRecipeItem& Item = N.RecipeInputs[ii];
					uint32 Avail = 0;
					for (const int32 Pid : N.DeviceInputPorts)
					{
						const FPort& IP = Ports[Pid];
						if (IP.SignalType == Item.SignalType)
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
							if (IP.SignalType != Item.SignalType)
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
		// TODO 步骤④：Latch / Counter / Clock（读写 FEtherDeviceStateFragment）

		if (bChanged)
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
	FPort& P = Ports[PortId];

	// ① 内部布线：本端口 -> 目标端口（分流：按 LinkTargets 顺序，灌满一个再下一个）
	if (P.Ether > 0)
	{
		for (const int32 ToPort : P.LinkTargets)
		{
			if (P.Ether == 0)
			{
				break;
			}
			FPort& Q = Ports[ToPort];
			const uint16 Rem = (Q.Throughput > Q.Ether)
				? static_cast<uint16>(Q.Throughput - Q.Ether) : 0;
			if (Rem == 0)
			{
				continue;
			}
			if (Q.Ether > 0 && Q.SignalType != P.SignalType)
			{
				continue; // TODO 步骤③：改成"同族异号湮灭 / 异族堵死"
			}
			uint16 Move = static_cast<uint16>(FMath::Min<uint32>(P.Ether, Rem));
			Move = static_cast<uint16>(FMath::Min<uint32>(Move, FMath::Min<uint32>(P.Throughput, Q.Throughput)));
			if (Move == 0)
			{
				continue;
			}
			if (Q.Ether == 0)
			{
				Q.SignalType = P.SignalType;
			}
			P.Ether = static_cast<uint16>(P.Ether - Move);
			Q.Ether = static_cast<uint16>(Q.Ether + Move);
		}
	}

	// ② 外部推送：出端口 -> 对端入端口（1:1）
	if (P.Ether > 0 && P.OutEdge != INDEX_NONE)
	{
		FPort& Q = Ports[Edges[P.OutEdge].ToPort];
		const uint16 Rem = (Q.Throughput > Q.Ether)
			? static_cast<uint16>(Q.Throughput - Q.Ether) : 0;
		if (Rem > 0 && !(Q.Ether > 0 && Q.SignalType != P.SignalType))
		{
			uint16 Move = static_cast<uint16>(FMath::Min<uint32>(P.Ether, Rem));
			Move = static_cast<uint16>(FMath::Min<uint32>(Move, FMath::Min<uint32>(P.Throughput, Q.Throughput)));
			if (Move > 0)
			{
				if (Q.Ether == 0)
				{
					Q.SignalType = P.SignalType;
				}
				P.Ether = static_cast<uint16>(P.Ether - Move);
				Q.Ether = static_cast<uint16>(Q.Ether + Move);
			}
		}
	}

	// ③ 判空：本端口还有料 → 本段保持活跃
	if (P.Ether > 0)
	{
		bOutHasEther = true;
	}
}
