// Fill out your copyright notice in the Description page of Project Settings.
#include "AsceFlowFieldCores.h"

DEFINE_LOG_CATEGORY(LogFlowField);

const FSector::FSectorID FSector::InvalidSectorID = FSector::FSectorID();
const FIslandField FSector::InvalidIslandField = FIslandField();
FPortal FWorldMapData::InvalidPortal = FPortal::FPortal({0, FSector::InvalidSectorID, FSector::InvalidIslandField, 0});

FSector::FSectorID FPortal::GetLinkToSectorID(const FWorldMapData& WorldMapData) const
{
	FIntVector2 OwingSectorLocation = WorldMapData.GetSectorLocation(OwingSectorID);
	switch (GetPortalType())
	{
		case EPortalType::Left :
		{
			return WorldMapData.GridLocationToSectorId(FIntVector2(OwingSectorLocation.X - SECTOR_SIZE, OwingSectorLocation.Y));
		};
		case EPortalType::Up :
		{
			return WorldMapData.GridLocationToSectorId(FIntVector2(OwingSectorLocation.X, OwingSectorLocation.Y + SECTOR_SIZE));		
		}
		case EPortalType::Right :
		{
			return WorldMapData.GridLocationToSectorId(FIntVector2(OwingSectorLocation.X + SECTOR_SIZE, OwingSectorLocation.Y));		
		}
		case EPortalType::Down :
		{
			return WorldMapData.GridLocationToSectorId(FIntVector2(OwingSectorLocation.X, OwingSectorLocation.Y - SECTOR_SIZE));		
		}
		default:
		{
			return FSector::InvalidSectorID;		
		}
	}
}

const FPortal& FPortal::GetLinkToPortal(const FWorldMapData& WorldMapData) const
{
	FSector::FSectorID LinkToSectorID = GetLinkToSectorID(WorldMapData);
	if (!LinkToSectorID.IsValid())
	{
		return FWorldMapData::InvalidPortal;
	}
	FPortal::FPortalIndex Start, End;
	WorldMapData.GetSectorPortalIndicesRange(LinkToSectorID, Start, End);
	for(uint32 Index = Start.RawIndex; Index < End.RawIndex; ++Index)
	{
		const FPortal& Portal = WorldMapData.Portals[Index];
		if (IsLinkable(Portal))
		{
			return Portal;
		}
	}
	return FWorldMapData::InvalidPortal;
}

FPortal::FPortalIndex FPortal::GetLinkToPortalIndex(const FWorldMapData& WorldMapData) const
{
	FSector::FSectorID LinkToSectorID = GetLinkToSectorID(WorldMapData);
	if (!LinkToSectorID.IsValid())
	{
		return FPortal::FPortalIndex();
	}
	FPortal::FPortalIndex Start, End;
	WorldMapData.GetSectorPortalIndicesRange(LinkToSectorID, Start, End);
	for (uint32 Index = Start.RawIndex; Index < End.RawIndex; ++Index)
	{
		const FPortal& Portal = WorldMapData.Portals[Index];
		if (IsLinkable(Portal))
		{
			return FPortal::FPortalIndex(Index);
		}
	}
	return FPortal::FPortalIndex();
}

void FCostFieldReplicated::PreReplicatedRemove(const struct FWorldMapData& InArraySerializer)
{
}

void FCostFieldReplicated::PostReplicatedAdd(const struct FWorldMapData& InArraySerializer)
{
}

void FCostFieldReplicated::PostReplicatedChange(const struct FWorldMapData& InArraySerializer)
{
}

void FWorldMapData::InitWorldMapData(uint8 InSectorCountX, uint8 InSectorCountY)
{
	SectorCountX = InSectorCountX;
	SectorCountY = InSectorCountY;
	GridCountX = static_cast<uint32>(InSectorCountX) * SECTOR_SIZE;
	GridCountY = static_cast<uint32>(InSectorCountY) * SECTOR_SIZE;

	const int32 SectorNum = static_cast<int32>(InSectorCountX) * static_cast<int32>(InSectorCountY);
	const int32 GridNum = static_cast<int32>(GridCountX) * static_cast<int32>(GridCountY);

	// 0号SectorData是共享的PlainSectorData：全部Grid代价=1（可通行、无障碍），
	// 初始时所有Sector都指向它；直到某个Sector第一次被写入才会COW分裂出独占数据
	SectorDataList.Reset();
	FSector& PlainSectorData = SectorDataList.AddDefaulted_GetRef();
	PlainSectorData.SectorID = FSector::InvalidSectorID; // 共享数据本身不代表任何真实Sector
	PlainSectorData.CostFields.Init(FCostField{0}, SECTOR_SIZE * SECTOR_SIZE);
	PlainSectorData.IslandFields.Init(FIslandField(), SECTOR_SIZE * SECTOR_SIZE);

	SectorIDToIndexMap.Init(0, SectorNum);           // 全部先指向共享的0号数据
	SectorFlagsList.Init(FSector::FSectorFlags{}, SectorNum);
	SectorDirtyList.Init(false, SectorNum);
	SectorIDToPortalStartIndexMap.Init(FPortal::FPortalIndex(0), SectorNum + 1);

	CostFields.Reset();
	CostFields.SetNum(GridNum);
	for (FCostFieldReplicated& Field : CostFields)
	{
		Field.Cost = 0;
	}
	
}

FPortal::FPortalIndex FWorldMapData::GetPortalIndexByGridIndex(const uint32 GridIndex, bool* IsPortalGrid) const
{
	if (CostFields[GridIndex].IsWall())
	{
		*IsPortalGrid = false;
		return FPortal::FPortalIndex();
	}

	FIntVector2 GridLocation = GridIndexToGridLocation(GridIndex);
	
	FPortal::EPortalType PortalType = FPortal::EPortalType::Invalid;
	int BitmapPosition = 0;
	int RemainderX = GridLocation.X % SECTOR_SIZE;
	int RemainderY = GridLocation.Y % SECTOR_SIZE;
	if (RemainderX == 0)
	{
		PortalType = FPortal::EPortalType::Left;
		BitmapPosition = RemainderY;
	}
	else if (RemainderX == SECTOR_SIZE - 1)
	{
		PortalType = FPortal::EPortalType::Right;
		BitmapPosition = RemainderY;
	}
	else if (RemainderY == 0)
	{
		PortalType = FPortal::EPortalType::Down;
		BitmapPosition = RemainderX;
	}
	else if (RemainderY == SECTOR_SIZE - 1)
	{
		PortalType = FPortal::EPortalType::Up;
		BitmapPosition = RemainderX;
	}

	if (PortalType == FPortal::EPortalType::Invalid)
	{
		if (IsPortalGrid)
		{
			*IsPortalGrid = false;
		}
		return FPortal::FPortalIndex();
	}

	if (IsPortalGrid)
	{
		*IsPortalGrid = true;
	}
	
	uint16 Bitmap = 0b1000000000000000 >> BitmapPosition;
	FSector::FSectorID OwingSectorID = GridLocationToSectorId(GridLocation);
	TArray<FPortal::FPortalIndex, TInlineAllocator<4>> OutPortalIndices;
	FindPortalIndicesInSectorFast(OwingSectorID,[PortalType, Bitmap](const FPortal& Portal)
	{
		return Portal.GetPortalType() == PortalType && (Portal.PortalBitMap & Bitmap);
	},OutPortalIndices);

	if (OutPortalIndices.Num() == 1)
	{
		return OutPortalIndices[0];
	}
	
	return FPortal::FPortalIndex();
}

bool FWorldMapData::IsEdgeGrid(const uint32 GridIndex) const
{
	FIntVector2 GridLocation = GridIndexToGridLocation(GridIndex);
	int RemainderX = GridLocation.X % SECTOR_SIZE;
	int RemainderY = GridLocation.Y % SECTOR_SIZE;
	return RemainderX == 0 || RemainderX == SECTOR_SIZE - 1 || RemainderY == 0 || RemainderY == SECTOR_SIZE - 1;
}

void FWorldMapData::FindPortalIndicesInSectorIsland(FSector::FSectorID SectorID, FIslandField IslandField,
                                                    TArray<FPortal::FPortalIndex>& PortalIndices) const
{
	FindPortalIndicesInSectorFast(SectorID, [IslandField](const FPortal& Portal)
	{
		return Portal.OwingIslandField == IslandField;
	},
	PortalIndices);	
}

void FWorldMapData::FindPortalIndicesInSector(FSector::FSectorID SectorID, TArray<FPortal::FPortalIndex>& PortalIndices) const
{
	FindPortalIndicesInSectorFast(SectorID, [](const FPortal& Portal)
	{
		return true;
	},
	PortalIndices);
}

FPortal::EPortalType FWorldMapData::GetPossiblePortalType(FIntVector2 GridLocation)
{
	FPortal::EPortalType PortalType = FPortal::EPortalType::Invalid;
	int RemainderX = GridLocation.X % SECTOR_SIZE;
	int RemainderY = GridLocation.Y % SECTOR_SIZE;
	if (RemainderX == 0)
	{
		PortalType = FPortal::EPortalType::Left;
	}
	else if (RemainderX == SECTOR_SIZE - 1)
	{
		PortalType = FPortal::EPortalType::Right;
	}
	else if (RemainderY == 0)
	{
		PortalType = FPortal::EPortalType::Down;
	}
	else if (RemainderY == SECTOR_SIZE - 1)
	{
		PortalType = FPortal::EPortalType::Up;
	}

	return PortalType;
}

int FWorldMapData::GetEdgeGridStartIndex(const FSector& TargetSector, const FPortal::EPortalType EdgeType)
{
	switch (EdgeType)
	{
		case FPortal::EPortalType::Up:    return TargetSector.GetUpEdgeGridStartIndex();
		case FPortal::EPortalType::Down:  return TargetSector.GetDownEdgeGridStartIndex();
		case FPortal::EPortalType::Left:  return TargetSector.GetLeftEdgeGridStartIndex();
		case FPortal::EPortalType::Right: return TargetSector.GetRightEdgeGridStartIndex();
		default: return 0;
	}
}

void FWorldMapData::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize)
{
}

void FWorldMapData::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize)
{
	PostReplicatedChange(AddedIndices, FinalSize);
}

void FWorldMapData::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize)
{
	for (int32 ChangedIndex : ChangedIndices)
	{
		PendingUpdateCostFields.Add({static_cast<uint32>(ChangedIndex), CostFields[ChangedIndex].Cost});
	}
}

void FWorldMapData::UpdateWorld(float DeltaTime)
{
	ProcessCostFields(DeltaTime);

	// 墙结构变化（OnCostFieldUpdate把相关扇区加入PendingWallPushSectors）：
	// ProcessCostFields消化完之后统一重建一次格簇，避免批量变墙时每格都算一遍
	if (PendingWallPushSectors.Num() > 0)
	{
		RebuildWallClusters();
		PendingWallPushSectors.Reset();
	}

	ProcessSectors(DeltaTime);

	ProcessPortals(DeltaTime);

	ProcessIslandFields(DeltaTime);
}

void FWorldMapData::RebuildWallClusters()
{
	const int32 GridNum = static_cast<int32>(GridCountX) * static_cast<int32>(GridCountY);
	const int32 GridCountXInt = static_cast<int32>(GridCountX);
	const int32 GridCountYInt = static_cast<int32>(GridCountY);
	if (CostFields.Num() != GridNum)
	{
		return;
	}

	auto IsWall = [this](int32 GridIndex) -> bool
	{
		return (GridIndex >= 0 && GridIndex < CostFields.Num())
			&& (CostFields[GridIndex].IsWall());
	};

	// 批次开始清空全局 bitmap（1bit/格；0=未并入任何簇）
	WallGridBitmap.Init(false, GridNum);
	auto& Bmp = WallGridBitmap;

	// 新簇列表（重建为"全量"：把本次涉及扇区的墙格全部重新归簇。
	// 注意：跨扇区连通墙区可能延伸出"待处理扇区"范围，这里按全图重扫一遍墙格归簇，
	// 扇区队列只是触发入口（扇区级增量需要更多状态，本次先全量——4096格(4x4)全扫微秒级）。
	// 若后续大图性能紧张，再改回"只归受影响墙区"。
	WallClusters.Reset();

	// 格→簇查找表：全置 -1（无墙簇），下方归簇时顺手填充
	GridIndexToCluster.Init(-1, GridNum);

	// 贪心归簇：从左下到右上扫描所有墙格，
	// 遇到"未归簇的墙格"→ 以它为起点向右扩展连续墙格成一行run → 再向下扩展行，
	// 每一行都取"与上一行相同的起始X开始、连续能延展的最大区间"——这是一个典型的
	// 最大矩形合并（greedy best rectangle），保证簇内无洞、水平垂直都撑满。
	// 行列扩展的定义（贪心）：先水平找这一行能连的最右格Xc，
	// 然后向下每次找"以Xc内连续"新行……为避免复杂，用简单两级扩展：
	//   1) 水平：从(StartX, RunY)向右扩到墙断开处 → [StartX, EndX)
	//   2) 垂直：向下每行检查 该行[StartX, EndX) 是否全部是墙，
	//            全墙就并入簇（高度+1，若行内有洞则停止本轮垂直扩展）
	// 这样得到的是"水平连续 + 垂直连续等宽"的矩形——对L/T/十字能拆成多个矩形，
	// 且每个矩形都是最大撑满的。
	for (int32 Y = 0; Y < GridCountYInt; ++Y)
	{
		for (int32 X = 0; X < GridCountXInt; ++X)
		{
			const int32 Index = Y * GridCountXInt + X;
			if (Bmp[Index])
			{
				continue;
			}
			if (!IsWall(Index))
			{
				continue;
			}

			// 1) 水平扩展找右界（闭区间 X..RightX）
			int32 RightX = X;
			while (RightX + 1 < GridCountXInt)
			{
				const int32 RIdx = Y * GridCountXInt + (RightX + 1);
				if (Bmp[RIdx] || !IsWall(RIdx))
				{
					break;
				}
				++RightX;
			}

			// 2) 垂直扩展：向下的每一行，[X..RightX] 全部是墙且未归簇 → 并入
			int32 BottomY = Y;
			while (BottomY + 1 < GridCountYInt)
			{
				bool bWholeRowWall = true;
				for (int32 CX = X; CX <= RightX; ++CX)
				{
					const int32 RIdx = (BottomY + 1) * GridCountXInt + CX;
					if (Bmp[RIdx] || !IsWall(RIdx))
					{
						bWholeRowWall = false;
						break;
					}
				}
				if (!bWholeRowWall)
				{
					break;
				}
				++BottomY;
			}

			// 记录簇（世界坐标AABB）
			FWallCluster Cluster;
			Cluster.Min = FVector2D(X * GRID_SIZE, Y * GRID_SIZE);
			Cluster.Max = FVector2D((RightX + 1) * GRID_SIZE, (BottomY + 1) * GRID_SIZE);
			const int32 ClusterIndex = WallClusters.Add(Cluster);

			// bitmap 标记簇内所有格子 + 填充格→簇查找表（O(1)索引，Solver用）
			for (int32 CY = Y; CY <= BottomY; ++CY)
			{
				for (int32 CX = X; CX <= RightX; ++CX)
				{
					const int32 CellIndex = CY * GridCountXInt + CX;
					Bmp[CellIndex] = true;
					GridIndexToCluster[CellIndex] = ClusterIndex;
				}
			}

			// 继续扫描（坐标已标记，下一个未归簇格被跳过后自然落在簇外）
		}
	}
}

void FWorldMapData::ProcessCostFields(float DeltaTime)
{
	int ProcessNum = 0;
	int CheckNum = 0;
	int MaxCheckNum = MaxCostFieldProcessNumPerTick * 2;
	while (PendingUpdateCostFields.Num() > 0)
	{
		if (ProcessNum >= MaxCostFieldProcessNumPerTick)
		{
			break;
		}

		if (CheckNum >= MaxCheckNum)
		{
			break;
		}
		
		CheckNum++;
		FPendingUpdateCostField UpdateCostField = PendingUpdateCostFields.Pop(EAllowShrinking::No);
		if (UpdateCostField.NewCost != CostFields[UpdateCostField.GridIndex].Cost)
		{
			//真实值和更新值不一致，说明pending里面的是旧数据，不必处理，直接抛弃吧
			continue;
		}
		ProcessNum++;
		OnCostFieldUpdate(UpdateCostField.GridIndex, CostFields[UpdateCostField.GridIndex].Cost);
	}

}

void FWorldMapData::ProcessSectors(float DeltaTime)
{
	int ProcessNum = 0;
	int CheckNum = 0;
	// 和ProcessCostFields一样的安全阀：PendingProcessSectors里可能有大量"弹出即因TimeStamp过期而被丢弃"的旧条目
	// (比如同一个Sector这一帧被标脏又处理了好几次)，只靠ProcessNum(只统计真正处理成功的)限流的话，
	// 这些无效条目会被无限制地一次性弹空，起不到分摊到多帧的效果，所以额外用CheckNum限制"总共弹出检查的次数"
	const int MaxCheckNum = MaxSectorProcessNumPerTick * 2;
	while (PendingProcessSectors.Num() > 0)
	{
		if (ProcessNum >= MaxSectorProcessNumPerTick)
		{
			break;
		}

		if (CheckNum >= MaxCheckNum)
		{
			break;
		}

		CheckNum++;
		if (OnSectorProcess(PendingProcessSectors.Pop()))
		{
			ProcessNum++;
		}
	}
}

void FWorldMapData::ProcessPortals(float DeltaTime)
{
	if (PendingAddPortalsThisTick.Num() == 0 && !bHasPendingPortalRemovals)
	{
		//这一帧完全没有Portal变更，直接跳过，零开销
		return;
	}

	PortalMergeScratchBuffer.Reset();   //只清空Num，保留已分配的容量
	PortalMergeScratchBuffer.Reserve(Portals.Num() + PendingAddPortalsThisTick.Num());

	int32 OldIndex = 0;
	int32 NewIndex = 0;
	const int32 OldNum = Portals.Num();
	const int32 NewNum = PendingAddPortalsThisTick.Num();

	while (OldIndex < OldNum || NewIndex < NewNum)
	{
		while (OldIndex < OldNum && Portals[OldIndex].IsPendingRemove())
		{
			OnPortalRemoved(Portals[OldIndex]);
			++OldIndex;
		}

		if (OldIndex >= OldNum)
		{
			while (NewIndex < NewNum)
			{
				OnPortalAdded(PendingAddPortalsThisTick[NewIndex]);
				PortalMergeScratchBuffer.Add(PendingAddPortalsThisTick[NewIndex]);
				++NewIndex;
			}
			break;
		}

		if (NewIndex >= NewNum)
		{
			PortalMergeScratchBuffer.Add(Portals[OldIndex]);
			++OldIndex;
			continue;
		}

		if (Portals[OldIndex].OwingSectorID <= PendingAddPortalsThisTick[NewIndex].OwingSectorID)
		{
			PortalMergeScratchBuffer.Add(Portals[OldIndex]);
			++OldIndex;
		}
		else
		{
			OnPortalAdded(PendingAddPortalsThisTick[NewIndex]);
			PortalMergeScratchBuffer.Add(PendingAddPortalsThisTick[NewIndex]);
			++NewIndex;
		}
	}

	Swap(Portals, PortalMergeScratchBuffer);
	PendingAddPortalsThisTick.Reset();
	bHasPendingPortalRemovals = false;

	UpdateSectorIDToPortalStartIndexMap();

#if WITH_EDITOR
	// Portal结构变化，通知调试器重写纹理
	OnStructureChanged.Broadcast();
#endif
}

void FWorldMapData::ProcessIslandFields(float DeltaTime)
{
	int ProcessNum = 0;
	int CheckNum = 0;
	// 同样的安全阀：TimeStamp校验失败的条目(比如Sector在入队之后又被别的更新抢先处理过)不该被无限制一次性弹空
	const int MaxCheckNum = MaxIslandFieldProcessNumPerTick * 2;
	while (PendingProcessIslandSectorsThisTick.Num() > 0)
	{
		if (ProcessNum >= MaxIslandFieldProcessNumPerTick)
		{
			break;
		}

		if (CheckNum >= MaxCheckNum)
		{
			break;
		}

		CheckNum++;
		FSector::FSectorID SectorID = PendingProcessIslandSectorsThisTick.Pop();
		FSector& Sector = GetMutableSectorData(SectorID);
		if (Sector.CheckTimeStamp(SectorID))
		{
			ProcessIslandFieldForSector(Sector);
			ProcessNum++;
		}
	}
}

void FWorldMapData::OnPortalAdded(FPortal& Portal)
{
}

void FWorldMapData::OnPortalRemoved(FPortal& Portal)
{
}

void FWorldMapData::OnCostFieldUpdate(uint32 GridIndex, uint8 NewCost)
{
	FIntVector2 GridLocation = GridIndexToGridLocation(GridIndex);
	FSector::FSectorID SectorID = GridLocationToSectorId(GridLocation);
	FSector& Sector = GetMutableSectorData(SectorID);
	bool bGridBlockStateChanged = Sector.UpdateCostField(GridLocationToGridIndexInSector(GridLocation), NewCost);

#if WITH_EDITOR
	// 通知调试器：该格代价变化，增量更新纹理像素
	OnCostFieldChanged.Broadcast(GridIndex);
#endif

	if (!bGridBlockStateChanged)
	{
		return;
	}

	// 墙状态变化：把该格所属扇区加入待处理集合（TSet自动去重——一帧内同扇区多格变墙只处理一次），
	// 统一在UpdateWorld的ProcessCostFields之后一次性RebuildWallClusters。
	// 注意一定要在SectorID被下面覆盖前记录（下面会替换成Sector.SectorID，虽然值一样但语义不同）。
	// 两端都重建：格簇不随CostFields复制（非网络数据），Server编辑翻墙/Client收到复制翻墙，
	// 各自基于自己的CostFields做确定性合并（纯格合并bit-exact，两端结果一致）。
	{
		FSector::FSectorID OriginalSectorID = SectorID;
		PendingWallPushSectors.Add(OriginalSectorID);
	}

	SectorID = Sector.SectorID;
	
	if (IsEdgeGrid(GridIndex))
	{
		MarkSectorEdgeDirty(SectorID, GetPossiblePortalType(GridLocation));
	}

	if (!IsSectorDirty(SectorID))
	{
		Sector.SectorID.TimeStamp++;
		MarkSectorDirty(SectorID);
		PendingProcessSectors.Add(Sector.SectorID);
	}
}

bool FWorldMapData::OnSectorProcess(FSector::FSectorID SectorID)
{
	//完整更新该扇区内的一切
	const FSector& Sector = GetSectorData(SectorID);
	if (Sector.CheckTimeStamp(SectorID))
	{
		//通过时间线校验，真正的更新流程

		//处理边上的传送门，这会导致邻居Sector被标脏并且进入待处理队列。
		//注意：ProcessSectorPortalsOnEdge内部处理邻居时可能触发COW，导致SectorDataList扩容，
		//这会让上面拿到的Sector引用失效，所以下面不能再读Sector，只能用参数SectorID(此时已经过CheckTimeStamp校验，
		//等价于Sector.SectorID)
		ProcessSectorPortalsOnEdge(Sector);


		//加入这一帧等待重新处理IslandField的列表
		PendingProcessIslandSectorsThisTick.Add(SectorID);
		
		//清理自身的脏标记。
		SectorDirtyList[SectorID.RawID] = false;
		return true;
	}

	return false;
}


void FWorldMapData::ProcessSectorPortalsOnEdge(const FSector& Sector)
{
	FSector::FSectorFlags& SectorFlags = SectorFlagsList[Sector.SectorID.RawID];

	if (!SectorFlags.IsEdgeDirty())
	{
		//边上的Grid都是没有变化的，不需要处理传送门的生成和销毁
		return;
	}

	FIntVector2 SectorLocation = GetSectorLocation(Sector.SectorID);
	// 到这里为止，Sector自己肯定已经不是共享的Plain数据了(CheckTimeStamp在OnSectorProcess里已经校验过)，
	// 只记它的SectorID，进lambda之后再按需重新取索引/引用，避免处理邻居时触发的COW把这里缓存的引用弄悬空
	const FSector::FSectorID SelfSectorID = Sector.SectorID;

	auto ProcessEdge = [this, SectorLocation, SelfSectorID](FPortal::EPortalType EdgeType)
	{
		FPortal::EPortalType NeighborType;
		FIntVector2 NeighborOffset;
		int32 IndexStride;

		switch (EdgeType)
		{
			case FPortal::EPortalType::Up:    NeighborType = FPortal::EPortalType::Down;  NeighborOffset = {0, SECTOR_SIZE};  IndexStride = 1; break;
			case FPortal::EPortalType::Down:  NeighborType = FPortal::EPortalType::Up;    NeighborOffset = {0, -SECTOR_SIZE}; IndexStride = 1; break;
			case FPortal::EPortalType::Left:  NeighborType = FPortal::EPortalType::Right; NeighborOffset = {-SECTOR_SIZE, 0}; IndexStride = SECTOR_SIZE; break;
			case FPortal::EPortalType::Right: NeighborType = FPortal::EPortalType::Left;  NeighborOffset = {SECTOR_SIZE, 0};  IndexStride = SECTOR_SIZE; break;
			default: return;
		}
		FSector::FSectorID NeighborSectorID = GridLocationToSectorId(SectorLocation + NeighborOffset);
		if (!NeighborSectorID.IsValid())
		{
			return;
		}

		// 自己和邻居都先各自拿一次索引(可能各自触发一次COW、导致SectorDataList扩容)，
		// 等两次都做完之后再统一取引用——这两个引用只在本次ProcessEdge调用内有效
		const uint16 SelfIndex = GetOrCreateMutableSectorIndex(SelfSectorID);
		const uint16 NeighborIndex = GetOrCreateMutableSectorIndex(NeighborSectorID);
		FSector& InSector = SectorDataList[SelfIndex];
		FSector& NeighborSector = SectorDataList[NeighborIndex];

		int SelfStartIndex = GetEdgeGridStartIndex(InSector, EdgeType);
		int NeighborStartIndex = GetEdgeGridStartIndex(NeighborSector, NeighborType);
		
		//先获取最终的传送门Bitmap
		uint16 PortalBitmap = 0;
		for (int i = 0; i < SECTOR_SIZE; i++)
		{
			int SelfGridIndex = SelfStartIndex + i * IndexStride;
			int NeighborGridIndex = NeighborStartIndex + i * IndexStride;
			if (!InSector.CostFields[SelfGridIndex].IsWall() && !NeighborSector.CostFields[NeighborGridIndex].IsWall())
			{
				PortalBitmap |= (1 << (SECTOR_SIZE - i - 1));
			}
		}
		if (EdgeType == FPortal::EPortalType::Up || EdgeType == FPortal::EPortalType::Down)
		{
			PortalBitmap &= 0b0111111111111110; // 强制清掉bit15、bit0
		}
		
		//可以证明对于SECTOR_SIZE = 16而言，一条边最多8个传送门
		TArray<FPortal::FPortalIndex, TInlineAllocator<8>> SelfPortalIndices;
		TArray<FPortal::FPortalIndex, TInlineAllocator<8>> NeighborPortalIndices;
		
		FindPortalIndicesInSectorFast(InSector.SectorID,[EdgeType](const FPortal& TargetPortal)
		{
			return TargetPortal.GetPortalType() == EdgeType;
		}, SelfPortalIndices);

		FindPortalIndicesInSectorFast(NeighborSector.SectorID,[NeighborType](const FPortal& TargetPortal)
		{
			return TargetPortal.GetPortalType() == NeighborType;
		}, NeighborPortalIndices);

		//每次更新Portal都是同时处理的，不可能上下两边的Portal对不上
		checkf(SelfPortalIndices.Num() == NeighborPortalIndices.Num(), TEXT("Portal not match!"));
		
		TArray<uint16, TInlineAllocator<8>> RequirePortalBitmaps;
		FPortal::ExtractPortalBitmaps(PortalBitmap, RequirePortalBitmaps);

		for (FPortal::FPortalIndex SelfIdx : SelfPortalIndices)
		{
			FPortal& SelfPortal = GetMutablePortal(SelfIdx);

			int32 FoundIdx = RequirePortalBitmaps.IndexOfByKey(SelfPortal.PortalBitMap);
			if (FoundIdx != INDEX_NONE)
			{
				// 这个bitmap还需要，保留，消费掉这个需求值
				RequirePortalBitmaps.RemoveAt(FoundIdx);
				continue;
			}

			// 不再需要，标记自己删除
			SelfPortal.MarkPendingRemove();
			bHasPendingPortalRemovals = true;

			// 邻居侧"值相同"的那个才是它的对面，不是NeighborPortalIndices[同一个i]
			bool bFoundNeighbor = false;
			for (FPortal::FPortalIndex NeighborIdx : NeighborPortalIndices)
			{
				FPortal& NeighborPortal = GetMutablePortal(NeighborIdx);
				if (NeighborPortal.PortalBitMap == SelfPortal.PortalBitMap)
				{
					NeighborPortal.MarkPendingRemove();
					bHasPendingPortalRemovals = true;
					bFoundNeighbor = true;
					break;
				}
			}
			checkf(bFoundNeighbor, TEXT("Portal pair missing on neighbor side!"));
		}

		for (int i = 0; i < RequirePortalBitmaps.Num(); i++)
		{
			int InsertIndex = Algo::UpperBoundBy(PendingAddPortalsThisTick, InSector.SectorID, &FPortal::OwingSectorID);
			PendingAddPortalsThisTick.Insert({RequirePortalBitmaps[i], InSector.SectorID, FIslandField(), static_cast<uint8>(EdgeType)}, InsertIndex);
			InsertIndex = Algo::UpperBoundBy(PendingAddPortalsThisTick, NeighborSector.SectorID, &FPortal::OwingSectorID);
			PendingAddPortalsThisTick.Insert({RequirePortalBitmaps[i], NeighborSector.SectorID, FIslandField(), static_cast<uint8>(NeighborType)}, InsertIndex);
		}

		switch (EdgeType)
		{
			case FPortal::EPortalType::Up: SectorFlagsList[InSector.SectorID.RawID].UpEdgeDirty = false; SectorFlagsList[NeighborSector.SectorID.RawID].DownEdgeDirty = false; break;
			case FPortal::EPortalType::Down: SectorFlagsList[InSector.SectorID.RawID].DownEdgeDirty = false; SectorFlagsList[NeighborSector.SectorID.RawID].UpEdgeDirty = false; break;
			case FPortal::EPortalType::Left: SectorFlagsList[InSector.SectorID.RawID].LeftEdgeDirty = false; SectorFlagsList[NeighborSector.SectorID.RawID].RightEdgeDirty = false; break;
			case FPortal::EPortalType::Right:SectorFlagsList[InSector.SectorID.RawID].RightEdgeDirty = false; SectorFlagsList[NeighborSector.SectorID.RawID].LeftEdgeDirty = false; break;
			default: break;
		}
		
		if (!SectorDirtyList[NeighborSector.SectorID.RawID])
		{
			//邻居不是脏的，说明邻居没有入队，直接入队更新吧
			MarkSectorDirty(NeighborSector.SectorID);
			NeighborSector.SectorID.TimeStamp++;
			PendingProcessSectors.Add(NeighborSector.SectorID);
		}
	};

	if (SectorFlags.UpEdgeDirty)
	{
		ProcessEdge(FPortal::EPortalType::Up);
	}

	if (SectorFlags.DownEdgeDirty)
	{
		ProcessEdge(FPortal::EPortalType::Down);
	}

	if (SectorFlags.RightEdgeDirty)
	{
		ProcessEdge(FPortal::EPortalType::Right);
	}

	if (SectorFlags.LeftEdgeDirty)
	{
		ProcessEdge(FPortal::EPortalType::Left);
	}
}

void FWorldMapData::ProcessIslandFieldForSector(FSector& Sector)
{
	for (FIslandField& Field : Sector.IslandFields)
	{
		Field.Reset();
	}

	FPortal::FPortalIndex PortalStart, PortalEnd;
	GetSectorPortalIndicesRange(Sector.SectorID, PortalStart, PortalEnd);
	for (uint32 Index = PortalStart.RawIndex; Index < PortalEnd.RawIndex; Index++)
	{
		Portals[Index].OwingIslandField.Reset();
	}

	auto FindOwningPortalIndex = [this, PortalStart, PortalEnd](int32 LocalX, int32 LocalY) -> FPortal::FPortalIndex
	{
		FPortal::EPortalType EdgeType = FPortal::EPortalType::Invalid;
		int32 BitPosition = 0;
		if (LocalX == 0)
		{
			EdgeType = FPortal::EPortalType::Left;
			BitPosition = LocalY;
		}
		else if (LocalX == SECTOR_SIZE - 1)
		{
			EdgeType = FPortal::EPortalType::Right;
			BitPosition = LocalY;
		}
		else if (LocalY == 0)
		{
			EdgeType = FPortal::EPortalType::Down;
			BitPosition = LocalX;
		}
		else if (LocalY == SECTOR_SIZE - 1)
		{
			EdgeType = FPortal::EPortalType::Up;
			BitPosition = LocalX;
		}
		else
		{
			return FPortal::FPortalIndex(); //不在边界上，不可能是Portal格子
		}

		uint16 Bitmap = 0b1000000000000000 >> BitPosition;
		for (uint32 Index = PortalStart.RawIndex; Index < PortalEnd.RawIndex; ++Index)
		{
			const FPortal& Portal = Portals[Index];
			if (Portal.GetPortalType() == EdgeType && (Portal.PortalBitMap & Bitmap))
			{
				return FPortal::FPortalIndex(Index);
			}
		}
		return FPortal::FPortalIndex();
	};

	uint8 NextIslandId = 0;
	TArray<int32, TInlineAllocator<SECTOR_SIZE * SECTOR_SIZE>> BFSQueue;
	static const FIntVector2 Offsets[4] = { {1,0},{-1,0},{0,1},{0,-1} };
	
	for (int32 SeedIndex = 0; SeedIndex < SECTOR_SIZE * SECTOR_SIZE; ++SeedIndex)
	{
		if (Sector.IslandFields[SeedIndex].IslandId != 0)
		{
			continue; // 已被之前的 BFS 覆盖
		}
		if (Sector.CostFields[SeedIndex].IsWall())
		{
			continue; // 墙
		}

		++NextIslandId;
		Sector.IslandFields[SeedIndex] = FIslandField(NextIslandId);

		// 从当前格子开始 BFS 处理 IslandField
		BFSQueue.Reset();
		BFSQueue.Add(SeedIndex);

		int32 Head = 0;
		while (Head < BFSQueue.Num())
		{
			int32 CurrentIndex = BFSQueue[Head++];
			int32 CurrentX = CurrentIndex % SECTOR_SIZE;
			int32 CurrentY = CurrentIndex / SECTOR_SIZE;

			for (const FIntVector2& Offset : Offsets)
			{
				int32 NeighborX = CurrentX + Offset.X;
				int32 NeighborY = CurrentY + Offset.Y;
				if (NeighborX < 0 || NeighborX >= SECTOR_SIZE || NeighborY < 0 || NeighborY >= SECTOR_SIZE)
				{
					continue; //出Sector范围，跨Sector连通性交给Portal机制处理
				}

				int32 NeighborIndex = NeighborX + NeighborY * SECTOR_SIZE;
				if (Sector.CostFields[NeighborIndex].IsWall())
				{
					continue;
				}
				if (Sector.IslandFields[NeighborIndex].IslandId != 0)
				{
					continue;
				}

				Sector.IslandFields[NeighborIndex] = FIslandField(NextIslandId);
				BFSQueue.Add(NeighborIndex);

				// 波前走到边界格子时，若它属于某个 Portal，把该 Portal 并入当前 Island
				FPortal::FPortalIndex OwnerPortalIndex = FindOwningPortalIndex(NeighborX, NeighborY);
				if (OwnerPortalIndex.IsValid())
				{
					FPortal& OwnerPortal = GetMutablePortal(OwnerPortalIndex);
					if (OwnerPortal.OwingIslandField.IslandId == 0)
					{
						OwnerPortal.OwingIslandField = FIslandField{ NextIslandId };
					}
				}
			}
		}
	}

#if WITH_EDITOR
	// IslandField结构变化，通知调试器重写纹理
	OnStructureChanged.Broadcast();
#endif
}

void FWorldMapData::UpdateSectorIDToPortalStartIndexMap()
{
	check(SectorIDToPortalStartIndexMap.Num() > 0);
	uint32 PortalIndex = 0;
	const uint32 PortalNum = Portals.Num();
	const uint16 SectorCount = SectorIDToPortalStartIndexMap.Num() - 1;
	for (int i = 0; i < SectorCount; i++)
	{
		SectorIDToPortalStartIndexMap[i] = FPortal::FPortalIndex(PortalIndex);
		while (PortalIndex < PortalNum && Portals[PortalIndex].OwingSectorID.RawID == i)
		{
			PortalIndex++;
		}
	}

	SectorIDToPortalStartIndexMap[SectorCount] = FPortal::FPortalIndex(PortalNum);
}


