// Fill out your copyright notice in the Description page of Project Settings.


#include "AscePathFinder.h"

FMergedPathPlanHandle UPathFinder::RunMergeablePathRequest(const FVector2D& GoalWorldLocation,
	const TArray<FVector2D>& EntityWorldLocations, TArray<FPortalPathResult>* OutResults)
{
	FMergedPathPlanHandle Handle;
	if (WorldMapData == nullptr)
	{
		return Handle;
	}

	EnsurePortalSearchScratchSized();

	if (OutResults)
	{
		OutResults->Reset();
	}

	FSourcePosition GoalSourcePosition = GetSourcePosition(GoalWorldLocation);

	Handle.RawID = NextPlanRawID++;
	FMergedPathPlanEntry& Entry = PathPlans.Add(Handle.RawID);
	Entry.Plan.GoalSourcePosition = GoalSourcePosition;
	Entry.Plan.GoalWorldLocation = GoalWorldLocation;
	TMap<FPortal::FPortalIndex, FPortal::FPortalIndex>& NextPortalTowardsGoal = Entry.Plan.NextPortalTowardsGoal;
	TMap<FSourcePosition, FPortal::FPortalIndex>& SectorIslandToExitPortal = Entry.Plan.SectorIslandToExitPortal;
	
	TSet<FSourcePosition> SourcePositionSet;
	GetSourcePositionSet(EntityWorldLocations, SourcePositionSet);

	// Source位置→世界坐标 反查：多个entity可能同(扇区,島)，记首个出现坐标作为该Source近似位置，
	// 供GetPortalId在多个同岛Portal里选离单位近的那个（同一岛的units共用同一个出口选择依据）。
	TMap<FSourcePosition, FVector2D> SourcePositionToWorldLocation;
	for (const FVector2D& Location : EntityWorldLocations)
	{
		const FSourcePosition Pos = GetSourcePosition(Location);
		if (!SourcePositionToWorldLocation.Contains(Pos))
		{
			SourcePositionToWorldLocation.Add(Pos, Location);
		}
	}

	//跨Sector时才需要，惰性计算一次即可（同Sector的Source完全用不上它）
	FPortal::FPortalIndex GoalPortalIndex = FPortal::FPortalIndex();

	for (const FSourcePosition& SourcePos : SourcePositionSet)
	{
		FPortalPathResult LocalResult;
		FPortalPathResult& Result = OutResults ? OutResults->AddDefaulted_GetRef() : LocalResult;
		Result.SourcePosition = SourcePos;

		//同Sector：不需要Portal图，交给该Sector自己的FlowField，PortalChain留空即视为成功
		if (SourcePos.SectorID == GoalSourcePosition.SectorID && SourcePos.IslandField == GoalSourcePosition.IslandField)
		{
			Result.bSuccess = true;
			continue;
		}

		if (!GoalPortalIndex.IsValid())
		{
			GoalPortalIndex = GetPortalId(GoalSourcePosition, GoalWorldLocation);
		}
		if (!GoalPortalIndex.IsValid())
		{
			continue; //目标所在Sector没有可用Portal（孤立区域），跨Sector的Source都无法到达，bSuccess保持false
		}

		const FVector2D* SourceWorldLocation = SourcePositionToWorldLocation.Find(SourcePos);
		FPortal::FPortalIndex SourcePortalIndex = SourceWorldLocation
			? GetPortalId(SourcePos, *SourceWorldLocation)
			: FPortal::FPortalIndex();	// 反查不到坐标（理论上不该发生——SourcePos必来自EntityWorldLocations），按原逻辑返回无效
		if (!SourcePortalIndex.IsValid())
		{
			continue; //Source自己所在的位置没有可用Portal，无法跨出该Sector
		}

		Result.bSuccess = RunSingleMergeableAStar(SourcePortalIndex, GoalSourcePosition, GoalWorldLocation, NextPortalTowardsGoal, Result.PortalChain);

		if (Result.bSuccess)
		{
#if WITH_EDITOR
			FString ChainStr;
			for (const FPortal::FPortalIndex& PIdx : Result.PortalChain)
			{
				const FPortal& P = WorldMapData->GetPortal(PIdx);
				ChainStr += FString::Printf(TEXT("P%d(S%d,%d I%d) "), PIdx.RawIndex,
					P.OwingSectorID.RawID % WorldMapData->GetSectorCountX(),
					P.OwingSectorID.RawID / WorldMapData->GetSectorCountX(),
					P.OwingIslandField.IslandId);
			}
			UE_LOG(LogFlowField, Warning, TEXT("[FFChain] Src(S%d,%d I%d) Chain: %s"),
				Result.SourcePosition.SectorID.RawID % WorldMapData->GetSectorCountX(),
				Result.SourcePosition.SectorID.RawID / WorldMapData->GetSectorCountX(),
				Result.SourcePosition.IslandField.IslandId,
				*ChainStr);
#endif

			for (int32 i = 0; i + 1 < Result.PortalChain.Num(); ++i)
			{
				if (!NextPortalTowardsGoal.Contains(Result.PortalChain[i]))
				{
					NextPortalTowardsGoal.Add(Result.PortalChain[i], Result.PortalChain[i + 1]);
				}
			}
			for (int32 i = 0; i < Result.PortalChain.Num(); )
			{
				int32 j = i;
				const FSector::FSectorID SectorID = WorldMapData->GetPortal(Result.PortalChain[i]).OwingSectorID;
				const FIslandField Island = WorldMapData->GetPortal(Result.PortalChain[i]).OwingIslandField;
				while (j + 1 < Result.PortalChain.Num() && WorldMapData->GetPortal(Result.PortalChain[j + 1]).OwingSectorID == SectorID)
				{
					++j;
				}
				SectorIslandToExitPortal.FindOrAdd(FSourcePosition(SectorID, Island)) = Result.PortalChain[j];
				i = j + 1;
			}
		}
	}

	return Handle;
}

void UPathFinder::AcquirePathPlan(FMergedPathPlanHandle Handle)
{
	if (FMergedPathPlanEntry* Entry = PathPlans.Find(Handle.RawID))
	{
		++Entry->RefCount;
	}
}

void UPathFinder::ReleasePathPlan(FMergedPathPlanHandle Handle)
{
	FMergedPathPlanEntry* Entry = PathPlans.Find(Handle.RawID);
	if (!Entry)
	{
		return;
	}
	--Entry->RefCount;
	if (Entry->RefCount <= 0)
	{
		//连带清空RetainedFlowFields持有的强引用；如果那之后真的没有别的地方还在用这些FlowFieldResult了，
		//FlowFieldCache里对应的弱引用条目会在下一次CleanupFlowFieldCache时被自然回收
		PathPlans.Remove(Handle.RawID);
	}
}


void UPathFinder::CleanupFlowFieldCache()
{
	// 先扫一遍PathPlans里引用计数<=0的"孤儿"Plan（正常情况下ReleasePathPlan归零时已经立即移除了，
	// 这里兜底的是RunMergeablePathRequest创建出来后一直没人调用过AcquirePathPlan/ReleasePathPlan的Plan）。
	// 必须放在FlowFieldCache清理之前——释放掉这些Plan持有的强引用后，
	// 才能让下面对WeakResult的判断反映出"真的没人再用了"这一最新状态
	for (auto It = PathPlans.CreateIterator(); It; ++It)
	{
		if (It.Value().RefCount <= 0)
		{
			It.RemoveCurrent();
		}
	}

	for (auto It = FlowFieldCache.CreateIterator(); It; ++It)
	{
		if (!It.Value().WeakResult.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

FVector2D UPathFinder::GetPortalRepresentativeWorldPosition(const FPortal& Portal) const
{
	uint32 Bitmap = static_cast<uint32>(Portal.PortalBitMap);
	int32 MinBit = FMath::CountTrailingZeros(Bitmap);
	int32 MaxBit = 31 - FMath::CountLeadingZeros(Bitmap);
	//跟ProcessIslandFieldForSector里的约定一致：bit号越大，沿边的局部坐标越小
	int32 CenterPosAlongEdge = SECTOR_SIZE - 1 - (MinBit + MaxBit) / 2;

	FIntVector2 SectorLocation = WorldMapData->GetSectorLocation(Portal.OwingSectorID);

	//沿边轴：用格子中心(+0.5)。两个互链Portal的PortalBitMap必须相同(见FPortal::IsLinkable)，
	//所以这个值在两侧天然一致，不受影响。
	//垂直轴：不用"自己这侧格子的中心"，而是直接用两个Sector的交界线坐标(不加0.5)——
	//这样跨Sector的两个"对面"Portal算出来的代表点会精确重合到同一个世界坐标，
	//而不是分别落在交界线两侧、相差一个GRID_SIZE的两个格子中心。
	//这从根源保证了heuristic的admissibility/consistency，"跨Sector边权重=0"这个设定才名副其实。
	switch (Portal.GetPortalType())
	{
		case FPortal::EPortalType::Left:
			return FVector2D(SectorLocation.X * GRID_SIZE,
				(SectorLocation.Y + CenterPosAlongEdge + 0.5f) * GRID_SIZE);
		case FPortal::EPortalType::Right:
			return FVector2D((SectorLocation.X + SECTOR_SIZE) * GRID_SIZE,
				(SectorLocation.Y + CenterPosAlongEdge + 0.5f) * GRID_SIZE);
		case FPortal::EPortalType::Down:
			return FVector2D((SectorLocation.X + CenterPosAlongEdge + 0.5f) * GRID_SIZE,
				SectorLocation.Y * GRID_SIZE);
		case FPortal::EPortalType::Up:
			return FVector2D((SectorLocation.X + CenterPosAlongEdge + 0.5f) * GRID_SIZE,
				(SectorLocation.Y + SECTOR_SIZE) * GRID_SIZE);
		default:
			return FVector2D((SectorLocation.X + 0.5f) * GRID_SIZE, (SectorLocation.Y + 0.5f) * GRID_SIZE);
	}
}

void UPathFinder::GetSourcePositionSet(const TArray<FVector2D>& EntityWorldLocations, TSet<FSourcePosition>& OutSet) const
{
	OutSet.Reset();
	for (const FVector2D& Location : EntityWorldLocations)
	{
		OutSet.Add(GetSourcePosition(Location));
	}
}

FPortal::FPortalIndex UPathFinder::GetPortalId(const FSourcePosition& SourcePosition, const FVector2D& SourceWorldLocation) const
{
	if (SourcePosition.IslandField.IslandId == 0)
	{
		return FPortal::FPortalIndex(); //不可寻路（墙格或无效位置；封闭岛会有非零ID，但遍历后找不到匹配Portal同样返回无效）
	}

	FPortal::FPortalIndex Start, End;
	WorldMapData->GetSectorPortalIndicesRange(SourcePosition.SectorID, Start, End);

	// 同岛多个Portal时选"代表点离source世界坐标最近"的作为起点/出口。
	// 因为同岛可达目标的多个Portal会共用同一个SectorIslandToExitPortal键，
	// A*起点=出口，所以起点选近的=entity实际去的是近的出口。
	FPortal::FPortalIndex BestPortalIndex;
	float BestDistSq = TNumericLimits<float>::Max();

	for (uint32 Index = Start.RawIndex; Index < End.RawIndex; ++Index)
	{
		if (WorldMapData->Portals[Index].OwingIslandField == SourcePosition.IslandField)
		{
			const FPortal::FPortalIndex Candidate(Index);
			const float DistSq = GetManhattanDistance(
				GetPortalRepresentativeWorldPosition(WorldMapData->GetPortal(Candidate)),
				SourceWorldLocation);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestPortalIndex = Candidate;
			}
		}
	}

	return BestPortalIndex;
}

void UPathFinder::GetPortalGraphNeighbors(FPortal::FPortalIndex PortalIndex, TArray<TPair<FPortal::FPortalIndex, float>>& OutNeighbors) const
{
	OutNeighbors.Reset();
	const FPortal& Portal = WorldMapData->GetPortal(PortalIndex);

	//同Sector同Island内的所有其他Portal，边权=两代表点曼哈顿距离
	TArray<FPortal::FPortalIndex> SameIslandPortals;
	WorldMapData->FindPortalIndicesInSectorIsland(Portal.OwingSectorID, Portal.OwingIslandField, SameIslandPortals);
	const FVector2D PortalPosition = GetPortalRepresentativeWorldPosition(Portal);
	for (const FPortal::FPortalIndex& OtherIndex : SameIslandPortals)
	{
		if (OtherIndex == PortalIndex)
		{
			continue;
		}
		float Distance = GetManhattanDistance(PortalPosition, GetPortalRepresentativeWorldPosition(WorldMapData->GetPortal(OtherIndex)));
		OutNeighbors.Add(TPair<FPortal::FPortalIndex, float>(OtherIndex, Distance));
	}

	//跨Sector的对面Portal，边权=0（同一个物理窗口的两个逻辑视角）
	FPortal::FPortalIndex LinkIndex = Portal.GetLinkToPortalIndex(*WorldMapData);
	if (LinkIndex.IsValid())
	{
		OutNeighbors.Add(TPair<FPortal::FPortalIndex, float>(LinkIndex, 0.f));
	}
}

void UPathFinder::ReconstructChain(FPortal::FPortalIndex EndIndex, FPortal::FPortalIndex StartIndex,
	const TArray<FPortalSearchNodeData>& Scratch, TArray<FPortal::FPortalIndex>& OutChain) const
{
	const FSector::FSectorID SourceSectorID = WorldMapData->GetPortal(StartIndex).OwingSectorID;

	// 单次回溯 + 压缩（头插保持正序）：
	// 同扇区连续段（逆序）——源扇区只保留段首(正序出口)；其他扇区保留段首+段尾(正序[入口,出口])；中间跳点跳过
	FPortal::FPortalIndex Cursor = EndIndex;
	FPortal::FPortalIndex PrevAdded = FPortal::FPortalIndex();
	bool bHasPrevAdded = false;

	while (true)
	{
		const FSector::FSectorID Sector = WorldMapData->GetPortal(Cursor).OwingSectorID;
		const FPortal::FPortalIndex CameFromCursor = (Cursor == StartIndex)
			? FPortal::FPortalIndex()
			: Scratch[Cursor.RawIndex].CameFrom;

		// 逆序段首（正序出口）：上一个保留节点跨扇区
		const bool bIsRunStart = !bHasPrevAdded
			|| WorldMapData->GetPortal(PrevAdded).OwingSectorID != Sector;
		// 逆序段尾（正序入口）：下一跳(朝Start)跨扇区或已到起点
		const bool bIsRunEnd = (Cursor == StartIndex)
			|| WorldMapData->GetPortal(CameFromCursor).OwingSectorID != Sector;

		const bool bKeep = (Sector == SourceSectorID) ? bIsRunStart : (bIsRunStart || bIsRunEnd);
		if (bKeep)
		{
			OutChain.Insert(Cursor, 0); // 头插，最终正序
		}

		PrevAdded = Cursor;
		bHasPrevAdded = true;

		if (Cursor == StartIndex)
		{
			break;
		}
		Cursor = CameFromCursor;
	}
}

void UPathFinder::AppendKnownSuffix(FPortal::FPortalIndex StartOfSuffix, const TMap<FPortal::FPortalIndex, FPortal::FPortalIndex>& NextPortalTowardsGoal,
	const FSector::FSectorID& SkipSector, TArray<FPortal::FPortalIndex>& OutChain) const
{
	// 追加或合并：若节点与链尾同扇区，当场压缩该扇区连续段——
	// 源扇区只保留出口；中间扇区保留[入口,出口]，中间跳点不进入链
	// "SkipSector"（只保留出口、跳过入口）这个特殊待遇，只对"整条合并链最前导、且从未离开过SkipSector"
	// 的那一段有效——判断依据是"进入本次AppendKnownSuffix调用时OutChain是否为空"：
	//  - 若为空，说明目前为止（含ReconstructChain已经处理过的前缀）从未离开过SkipSector，这是真正连续的
	//    "源所在Sector"那一段，允许跳过入口；
	//  - 若非空，说明前缀里已经出现过别的Sector了，之后哪怕SkipSector的SectorID又重新出现（比如墙把
	//    Sector劈成多个Island，需要绕道邻居Sector才能到达同一物理Sector里的另一个Island），也只是一次
	//    普通的"中途重新路过"，必须当成普通中间Sector对待（入口+出口都保留），不能再套用"跳过入口"的
	//    规则——否则会错误丢弃真正需要的入口标记，导致这一段的FlowField请求被漏掉
	bool bCanSkipEntry = (OutChain.Num() == 0);

	auto AppendOrMerge = [&](FPortal::FPortalIndex Portal)
	{
		const FSector::FSectorID Sector = WorldMapData->GetPortal(Portal).OwingSectorID;
		if (Sector != SkipSector)
		{
			bCanSkipEntry = false; // 一旦离开过SkipSector，之后永久失效，哪怕SectorID之后又对上
		}

		if (OutChain.Num() > 0 && WorldMapData->GetPortal(OutChain.Last()).OwingSectorID == Sector)
		{
			// 链尾连续同扇区段的起点（保留的入口）
			int32 SegStart = OutChain.Num() - 1;
			while (SegStart > 0 && WorldMapData->GetPortal(OutChain[SegStart - 1]).OwingSectorID == Sector)
			{
				--SegStart;
			}

			if (bCanSkipEntry && Sector == SkipSector)
			{
				// 真正连续未离开过的源扇区：只保留当前节点（出口）
				OutChain.SetNum(SegStart);
				OutChain.Add(Portal);
			}
			else
			{
				// 中间扇区（含"绕道后重新路过源扇区SectorID"的情况）：保留入口，当前节点追加为出口
				OutChain.SetNum(SegStart + 1);
				OutChain.Add(Portal);
			}
		}
		else
		{
			OutChain.Add(Portal);
		}
	};

	FPortal::FPortalIndex Cursor = StartOfSuffix;
	AppendOrMerge(Cursor);
	while (const FPortal::FPortalIndex* Next = NextPortalTowardsGoal.Find(Cursor))
	{
		Cursor = *Next;
		AppendOrMerge(Cursor);
	}
}

bool UPathFinder::RunSingleMergeableAStar(FPortal::FPortalIndex StartPortalIndex, const FSourcePosition& GoalSourcePosition, const FVector2D& GoalWorldLocation, 
	TMap<FPortal::FPortalIndex, FPortal::FPortalIndex>& NextPortalTowardsGoal, TArray<FPortal::FPortalIndex>& OutChain) const
{
	OutChain.Reset();

	//特殊情况：起点本身已经在某条已算出的路径上了，直接借用后半段，不用跑A*
	if (NextPortalTowardsGoal.Find(StartPortalIndex))
	{
		AppendKnownSuffix(StartPortalIndex, NextPortalTowardsGoal, WorldMapData->GetPortal(StartPortalIndex).OwingSectorID, OutChain);
		return true;
	}
	

	//每次搜索独立的一份"版本戳"，让上一次搜索遗留在scratch数组里的数据自动失效，
	//不需要对PortalSearchScratch做整体清空（O(全局Portal数)）。
	//前提：EnsurePortalSearchScratchSized()已经在RunMergeablePathRequest里跑过一次，
	//数组大小在本次搜索期间不会再变，下面这个lambda拿到的引用全程有效，不会因为SetNum而悬空。
	++CurrentSearchStamp;

	auto GetOrInitScratch = [this](FPortal::FPortalIndex PortalIndex) -> FPortalSearchNodeData&
	{
		FPortalSearchNodeData& Data = PortalSearchScratch[PortalIndex.RawIndex];
		if (Data.SearchStamp != CurrentSearchStamp)
		{
			Data.SearchStamp = CurrentSearchStamp;
			Data.BestG = TNumericLimits<float>::Max();
			Data.CameFrom = FPortal::FPortalIndex();
		}
		return Data;
	};

	// 启发式：到Goal实际世界位置的曼哈顿距离。admissible：格路径长度恒>=曼哈顿距离；
	// consistent：跨扇区链接边代表点重合(代价0)，同扇区边满足三角不等式
	auto Heuristic = [&](FPortal::FPortalIndex PortalIndex) -> float
	{
		return GetManhattanDistance(GetPortalRepresentativeWorldPosition(WorldMapData->GetPortal(PortalIndex)), GoalWorldLocation);
	};

	struct FOpenNode
	{
		FPortal::FPortalIndex PortalIndex;
		//从起点Portal到当前Portal需要的总权重（不一定是最优，靠懒删除过滤过期条目）
		float G;
		//从当前Portal走到GoalPortal最终的启发式权重
		float F;
	};
	auto HeapPredicate = [](const FOpenNode& A, const FOpenNode& B) { return A.F < B.F; };
	TArray<FOpenNode> OpenHeap;
	
	const float StartH = Heuristic(StartPortalIndex);
	GetOrInitScratch(StartPortalIndex).BestG = 0.f;
	OpenHeap.HeapPush(FOpenNode{ StartPortalIndex, 0.f, StartH }, HeapPredicate);

	TArray<TPair<FPortal::FPortalIndex, float>> Neighbors;

	while (OpenHeap.Num() > 0)
	{
		FOpenNode Current;
		OpenHeap.HeapPop(Current, HeapPredicate, EAllowShrinking::No);

		FPortalSearchNodeData& CurrentData = GetOrInitScratch(Current.PortalIndex);

		//懒删除：这个堆条目可能已经过期（后来找到了更便宜的G），直接丢弃重新弹下一个。
		//这里不再有独立的Closed集合——允许"reopening"，作为对齐修复之外的额外安全网。
		if (Current.G > CurrentData.BestG + KINDA_SMALL_NUMBER)
		{
			continue;
		}

		//终止条件1：寻路第一次到达Goal扇区Goal岛的Portal——
		//进入Goal扇区后由该扇区流场直达Goal格子，链上不再追加Goal扇区内的Portal
		const FPortal& CurrentPortal = WorldMapData->GetPortal(Current.PortalIndex);
		if (CurrentPortal.OwingSectorID == GoalSourcePosition.SectorID && CurrentPortal.OwingIslandField == GoalSourcePosition.IslandField)
		{
			ReconstructChain(Current.PortalIndex, StartPortalIndex, PortalSearchScratch, OutChain);
			return true;
		}

		GetPortalGraphNeighbors(Current.PortalIndex, Neighbors);
		for (const TPair<FPortal::FPortalIndex, float>& NeighborPair : Neighbors)
		{
			const FPortal::FPortalIndex NeighborIndex = NeighborPair.Key;
			// 合并：邻居已在某条已知路径上 → 立即合并借后缀
			// （展开时自然触发，覆盖原预检/弹出命中/扇区岛合并的所有场景）
			if (NextPortalTowardsGoal.Find(NeighborIndex))
			{
				const FSector::FSectorID StartSectorID = WorldMapData->GetPortal(StartPortalIndex).OwingSectorID;
				if (Current.PortalIndex == StartPortalIndex)
				{
					// 起点特判：链首必须是源扇区Portal
					if (WorldMapData->GetPortal(NeighborIndex).OwingSectorID == StartSectorID)
					{
						// 同岛邻居：空链开始，AppendOrMerge剥净链首
						AppendKnownSuffix(NeighborIndex, NextPortalTowardsGoal, StartSectorID, OutChain);
					}
					else
					{
						// 跨扇区链接对面：链首保留起点Portal
						OutChain.Add(StartPortalIndex);
						AppendKnownSuffix(NeighborIndex, NextPortalTowardsGoal, StartSectorID, OutChain);
					}
				}
				else
				{
					// 非起点：回溯当前节点的链（已剥净源扇区），再接邻居和后缀
					ReconstructChain(Current.PortalIndex, StartPortalIndex, PortalSearchScratch, OutChain);
					AppendKnownSuffix(NeighborIndex, NextPortalTowardsGoal, StartSectorID, OutChain);
				}
				return true;
			}
			
			float TentativeG = CurrentData.BestG + NeighborPair.Value;

			FPortalSearchNodeData& NeighborData = GetOrInitScratch(NeighborIndex);
			if (TentativeG >= NeighborData.BestG - KINDA_SMALL_NUMBER)
			{
				//这个邻居已经有一个不比当前差的G了，跳过
				continue; 
			}

			NeighborData.BestG = TentativeG;
			NeighborData.CameFrom = Current.PortalIndex;
			float H = Heuristic(NeighborIndex);
			OpenHeap.HeapPush(FOpenNode{ NeighborIndex, TentativeG, TentativeG + H }, HeapPredicate);
		}
	}

	//理论上不该走到这——上层已经用IslandField先做过可达性判断，这里只是防御性返回
	return false;
}

void UPathFinder::EnsurePortalSearchScratchSized() const
{
	const int32 RealPortalCount = WorldMapData->Portals.Num();
	if (PortalSearchScratch.Num() != RealPortalCount)
	{
		PortalSearchScratch.SetNum(RealPortalCount);
		CurrentSearchStamp = 0; //数组内容整体作废，SearchStamp从头计数即可
	}
}

/**
 * Eikonal Flow Field
 */

bool UPathFinder::TryGetFlowDirection(FMergedPathPlanHandle Handle, const FVector2D& EntityWorldLocation, FVector2D& OutDirection)
{
	if (WorldMapData == nullptr)
	{
		return false;
	}

	FMergedPathPlanEntry* Entry = PathPlans.Find(Handle.RawID);
	if (!Entry)
	{
		return false; //Handle无效，或者对应Plan已经被回收（比如所有entity都Release完了）
	}
	const FMergedPathPlan& Plan = Entry->Plan;

	const FSourcePosition CurrentSourcePos = GetSourcePosition(EntityWorldLocation);
	if (!CurrentSourcePos.SectorID.IsValid() || CurrentSourcePos.IslandField.IslandId == 0)
	{
		return false; // 地图范围外，或者落在墙/无效格子上
	}

	FFlowFieldTarget Target;
	if (CurrentSourcePos == Plan.GoalSourcePosition)
	{
		// 已经和Goal同Sector同Island：直接朝Goal的真实格子走，不需要经过任何Portal
		Target = FFlowFieldTarget(TInPlaceType<FIntVector2>(), WorldMapData->WorldLocationToGridLocation(Plan.GoalWorldLocation));
	}
	else
	{
		const FPortal::FPortalIndex* ExitPortal = Plan.SectorIslandToExitPortal.Find(CurrentSourcePos);
		if (!ExitPortal)
		{
			// 这个(Sector,Island)没有被这批Plan覆盖到（孤立区域或跑出规划范围），
			// 需要由调用方为它重新发起一次RunMergeablePathRequest
			return false;
		}
		Target = FFlowFieldTarget(TInPlaceType<FPortal::FPortalIndex>(), *ExitPortal);
	}

	const TSharedPtr<FFlowFieldResult> Result = GetOrComputeFlowField(CurrentSourcePos.SectorID.RawID, Target);
	if (!Result.IsValid())
	{
		return false;
	}

	// 让这个Plan攥住一份强引用，防止它在下一帧被查询之前就被FlowFieldCache的弱引用机制回收掉；
	// 用同样的Key覆盖旧值即可——如果Result因为CostField变化被重算过，这里会自动换成最新的那份
	Entry->RetainedFlowFields.FindOrAdd(FFlowFieldKey{CurrentSourcePos.SectorID.RawID, Target}) = Result;

	const FIntVector2 GridLocation = WorldMapData->WorldLocationToGridLocation(EntityWorldLocation);
	const uint32 LocalIndex = WorldMapData->GridLocationToGridIndexInSector(GridLocation);
	if (LocalIndex >= static_cast<uint32>(Result->Directions.Num()))
	{
		return false;
	}

	const FFlowField& Field = Result->Directions[LocalIndex];
	if (!Field.Pathable())
	{
		return false; // 墙/积分场没能覆盖到的死区格子
	}

	OutDirection = FFlowField::DirectionToVector(Field.GetDirection());
	return true;
}

TSharedPtr<FFlowFieldResult> UPathFinder::GetOrComputeFlowField(uint16 SectorRawID, const FFlowFieldTarget& Target)
{
	// 活体SectorID：GetSectorData().SectorID才带真实的TimeStamp，不能自己用FSectorID(RawID)现拼一个；
	// 但如果这个Sector还没被COW过(仍指向共享的0号Plain数据)，LiveSector.SectorID会是个哨兵值InvalidSectorID
	// (RawID=MAX_uint16)，不能拿去做GetSectorLocation等按RawID计算的定位用途，这种情况要退回用请求方传入的SectorRawID
	const FSector& LiveSector = WorldMapData->GetSectorData(FSector::FSectorID(SectorRawID));
	const FSector::FSectorID RealSectorID = LiveSector.SectorID.IsValid() ? LiveSector.SectorID : FSector::FSectorID(SectorRawID);

	FFlowFieldKey Key{ SectorRawID, Target };
	if (FFlowFieldCacheEntry* Entry = FlowFieldCache.Find(Key))
	{
		if (LiveSector.CheckTimeStamp(Entry->SectorIDAtComputeTime))
		{
			if (TSharedPtr<FFlowFieldResult> Pinned = Entry->WeakResult.Pin())
			{
				return Pinned; // 版本没过期，且还有人持有，直接复用
			}
		}
		// 过期，或者已经没人持有：走到下面原地重算并覆盖这个slot
	}

	TSharedPtr<FFlowFieldResult> NewResult = MakeShared<FFlowFieldResult>();
	ComputeFlowFieldInto(RealSectorID, Target, *NewResult);
	FlowFieldCache.Add(Key, FFlowFieldCacheEntry{ LiveSector.SectorID, NewResult });
	return NewResult;
}

namespace
{
	struct FLocalDir { int32 DX; int32 DY; };

	/** 计算障碍格Obstacle朝向来源方向(Dir)的那条近边上的两个顶点：
	 * VertexForPerp1对应"Perp1那一侧开放"该用的顶点，VertexForPerp2对应"Perp2那一侧开放"该用的顶点 */
	void GetNearEdgeVertices(const FIntVector2& Obstacle, const FLocalDir& Dir, FVector2D& OutVertexForPerp1, FVector2D& OutVertexForPerp2)
	{
		if (Dir.DX == 0)
		{
			// Dir是Up/Down：近边是水平边，Perp1=右邻居，Perp2=左邻居
			const float Y = (Dir.DY > 0) ? static_cast<float>(Obstacle.Y) : static_cast<float>(Obstacle.Y + 1);
			OutVertexForPerp1 = FVector2D(Obstacle.X + 1, Y);
			OutVertexForPerp2 = FVector2D(Obstacle.X, Y);
		}
		else
		{
			// Dir是Left/Right：近边是竖直边，Perp1=上邻居，Perp2=下邻居
			const float X = (Dir.DX > 0) ? static_cast<float>(Obstacle.X) : static_cast<float>(Obstacle.X + 1);
			OutVertexForPerp1 = FVector2D(X, Obstacle.Y + 1);
			OutVertexForPerp2 = FVector2D(X, Obstacle.Y);
		}
	}
}

void UPathFinder::RunLOSPass(const FSector& Sector, const FIntVector2& SeedLocalGridLocation, FIntegrationFieldArray& OutIntegrationFields) const
{
	OutIntegrationFields.SetNum(SECTOR_SIZE * SECTOR_SIZE);
	for (FIntegrationField& Field : OutIntegrationFields)
	{
		Field.TotalIntegratedCost = MAX_uint16;
		Field.IntegrationFlags = FIntegrationField::EIntegratedFlags::None;
	}

	if (!IsInsideSectorLocalBounds(SeedLocalGridLocation))
	{
		return;
	}

	static const FLocalDir Directions[4] = { {0, 1}, {1, 0}, {0, -1}, {-1, 0} }; // Up, Right, Down, Left

	TArray<FIntVector2> ActiveWave;
	ActiveWave.Reserve(SECTOR_SIZE * SECTOR_SIZE);

	const int32 SeedIndex = GetLocalGridIndex(SeedLocalGridLocation);
	OutIntegrationFields[SeedIndex].TotalIntegratedCost = 0;
	OutIntegrationFields[SeedIndex].IntegrationFlags = static_cast<FIntegrationField::EIntegratedFlags>(
		static_cast<uint8>(FIntegrationField::EIntegratedFlags::LineOfSight) | static_cast<uint8>(FIntegrationField::EIntegratedFlags::ActiveWaveFront));
	ActiveWave.Add(SeedLocalGridLocation);

	int32 ReadIndex = 0;
	while (ReadIndex < ActiveWave.Num())
	{
		const FIntVector2 Current = ActiveWave[ReadIndex++];
		const int32 CurrentIndex = GetLocalGridIndex(Current);
		const uint16 CurrentCost = OutIntegrationFields[CurrentIndex].TotalIntegratedCost;

		for (const FLocalDir& Dir : Directions)
		{
			const FIntVector2 Neighbor(Current.X + Dir.DX, Current.Y + Dir.DY);
			if (!IsInsideSectorLocalBounds(Neighbor))
			{
				continue; // 不跨Sector传播
			}

			const int32 NeighborIndex = GetLocalGridIndex(Neighbor);
			FIntegrationField& NeighborField = OutIntegrationFields[NeighborIndex];

			if ((static_cast<uint8>(NeighborField.IntegrationFlags) & static_cast<uint8>(FIntegrationField::EIntegratedFlags::LineOfSight)) != 0)
			{
				continue; // 已经处理过
			}
			if ((static_cast<uint8>(NeighborField.IntegrationFlags) & static_cast<uint8>(FIntegrationField::EIntegratedFlags::LOSWaveFrontBlocked)) != 0)
			{
				continue; // 已被阴影线挡住，不再传播LOS
			}

			const uint8 NeighborCost = Sector.CostFields[NeighborIndex].Cost;
			if (NeighborCost <= 1)
			{
				NeighborField.TotalIntegratedCost = CurrentCost + 1;
				NeighborField.IntegrationFlags = static_cast<FIntegrationField::EIntegratedFlags>(
					static_cast<uint8>(NeighborField.IntegrationFlags)
					| static_cast<uint8>(FIntegrationField::EIntegratedFlags::LineOfSight)
					| static_cast<uint8>(FIntegrationField::EIntegratedFlags::ActiveWaveFront));
				ActiveWave.Add(Neighbor);
				continue;
			}

			// NeighborCost > 1：障碍物候选，检查它自己在垂直于Dir方向上的两个邻居的开放情况
			FIntVector2 Perp1, Perp2;
			if (Dir.DX == 0)
			{
				Perp1 = FIntVector2(Neighbor.X + 1, Neighbor.Y); // 右
				Perp2 = FIntVector2(Neighbor.X - 1, Neighbor.Y); // 左
			}
			else
			{
				Perp1 = FIntVector2(Neighbor.X, Neighbor.Y + 1); // 上
				Perp2 = FIntVector2(Neighbor.X, Neighbor.Y - 1); // 下
			}

			auto IsBlocked = [&Sector](const FIntVector2& Loc) -> bool
			{
				if (!IsInsideSectorLocalBounds(Loc))
				{
					return true; // 越界视为阻挡，保守处理
				}
				return Sector.CostFields[GetLocalGridIndex(Loc)].Cost > 1;
			};

			const bool bPerp1Blocked = IsBlocked(Perp1);
			const bool bPerp2Blocked = IsBlocked(Perp2);
			if (bPerp1Blocked && bPerp2Blocked)
			{
				continue; // 两侧都挡：墙内部，不是角点
			}

			// 至少有一侧开放：是候选角点。但如果障碍继续沿对角线方向延伸（比如斜线台阶墙的中间台阶），
			// 说明这个顶点周围其实还是被墙"包住"的凹角，并没有露出新的视野缺口，不该画阴影线；
			// 只有对角格子也是空地时，才是真正凸出来的轮廓尖角
			FVector2D VertexForPerp1, VertexForPerp2;
			GetNearEdgeVertices(Neighbor, Dir, VertexForPerp1, VertexForPerp2);

			if (!bPerp1Blocked)
			{
				const FIntVector2 DiagonalForPerp1(Perp1.X + Dir.DX, Perp1.Y + Dir.DY);
				if (!IsBlocked(DiagonalForPerp1))
				{
					MarkShadowLine(SeedLocalGridLocation, VertexForPerp1, OutIntegrationFields);
				}
			}
			if (!bPerp2Blocked)
			{
				const FIntVector2 DiagonalForPerp2(Perp2.X + Dir.DX, Perp2.Y + Dir.DY);
				if (!IsBlocked(DiagonalForPerp2))
				{
					MarkShadowLine(SeedLocalGridLocation, VertexForPerp2, OutIntegrationFields);
				}
			}
		}
	}
}

void UPathFinder::MarkShadowLine(const FIntVector2& SeedLocalGridLocation, const FVector2D& CornerVertex, FIntegrationFieldArray& OutIntegrationFields)
{
	const FVector2D SeedCenter(SeedLocalGridLocation.X + 0.5f, SeedLocalGridLocation.Y + 0.5f);
	FVector2D Direction = CornerVertex - SeedCenter;
	if (Direction.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		return; // 顶点和种子几乎重合，方向未定义，理论上不该发生
	}
	Direction.Normalize();

	// CornerVertex正好卡在整数网格线上，往前挪一点点再取floor，避免落在哪个格子上产生歧义
	const FVector2D RayOrigin = CornerVertex + Direction * 0.001f;

	const int32 StepX = (Direction.X > 0.f) ? 1 : ((Direction.X < 0.f) ? -1 : 0);
	const int32 StepY = (Direction.Y > 0.f) ? 1 : ((Direction.Y < 0.f) ? -1 : 0);

	FIntVector2 CellCoord(FMath::FloorToInt(RayOrigin.X), FMath::FloorToInt(RayOrigin.Y));

	// Amanatides-Woo体素遍历：TMax为沿射线走到下一条格线所需的参数t，TDelta为每跨过一整格t需要增加多少
	constexpr float Infinite = TNumericLimits<float>::Max();
	float TMaxX = Infinite, TDeltaX = Infinite;
	if (StepX != 0)
	{
		TDeltaX = FMath::Abs(1.f / Direction.X);
		const float NextBoundaryX = static_cast<float>(StepX > 0 ? CellCoord.X + 1 : CellCoord.X);
		TMaxX = FMath::Abs((NextBoundaryX - RayOrigin.X) / Direction.X);
	}
	float TMaxY = Infinite, TDeltaY = Infinite;
	if (StepY != 0)
	{
		TDeltaY = FMath::Abs(1.f / Direction.Y);
		const float NextBoundaryY = static_cast<float>(StepY > 0 ? CellCoord.Y + 1 : CellCoord.Y);
		TMaxY = FMath::Abs((NextBoundaryY - RayOrigin.Y) / Direction.Y);
	}

	// 保守上限：整个Sector最多也就SECTOR_SIZE*2步左右能走出去，多给一倍余量防止浮点误差导致的死循环
	const int32 MaxSteps = SECTOR_SIZE * 4;
	for (int32 Step = 0; Step < MaxSteps; ++Step)
	{
		if (!IsInsideSectorLocalBounds(CellCoord))
		{
			break; // 出了Sector边界，停止
		}

		FIntegrationField& Field = OutIntegrationFields[GetLocalGridIndex(CellCoord)];
		Field.IntegrationFlags = static_cast<FIntegrationField::EIntegratedFlags>(
			static_cast<uint8>(Field.IntegrationFlags) | static_cast<uint8>(FIntegrationField::EIntegratedFlags::LOSWaveFrontBlocked));

		if (TMaxX < TMaxY)
		{
			CellCoord.X += StepX;
			TMaxX += TDeltaX;
		}
		else if (TMaxY < TMaxX)
		{
			CellCoord.Y += StepY;
			TMaxY += TDeltaY;
		}
		else
		{
			// 射线正好穿过格子的顶点(对角线情况)：两个方向都走一步，避免从对角缝隙里"漏"过去而不标记
			CellCoord.X += StepX;
			CellCoord.Y += StepY;
			TMaxX += TDeltaX;
			TMaxY += TDeltaY;
		}
	}
}

void UPathFinder::RunCostIntegrationPass(const FSector& Sector, const FFlowFieldTarget& Target, FIntegrationFieldArray& IntegrationFields) const
{
	if (IntegrationFields.Num() != SECTOR_SIZE * SECTOR_SIZE)
	{
		// Portal Target没跑过LOS Pass，IntegrationFields是空的，这里补一次全量初始化
		IntegrationFields.SetNum(SECTOR_SIZE * SECTOR_SIZE);
		for (FIntegrationField& Field : IntegrationFields)
		{
			Field.TotalIntegratedCost = MAX_uint16;
			Field.IntegrationFlags = FIntegrationField::EIntegratedFlags::None;
		}
	}

	static const FLocalDir Directions[4] = { {0, 1}, {1, 0}, {0, -1}, {-1, 0} };

	// 对应FIM论文Algorithm 3.2里的g(U,x)：用Loc当前已知的4个邻居代价(Godunov upwind的离散简化版，
	// 我们这里CostField只是标量代价而非速度场，所以直接是CostField[Loc]+min(邻居代价)，不需要解二次方程)
	auto ComputeCandidate = [&](const FIntVector2& Loc) -> uint16
	{
		const int32 Index = GetLocalGridIndex(Loc);
		const uint8 Cost = Sector.CostFields[Index].Cost;
		uint32 MinNeighbor = MAX_uint16;
		for (const FLocalDir& Dir : Directions)
		{
			const FIntVector2 Neighbor(Loc.X + Dir.DX, Loc.Y + Dir.DY);
			if (!IsInsideSectorLocalBounds(Neighbor))
			{
				continue; // 不跨Sector传播，越界视为"无穷远"，不参与取min
			}
			MinNeighbor = FMath::Min(MinNeighbor, static_cast<uint32>(IntegrationFields[GetLocalGridIndex(Neighbor)].TotalIntegratedCost));
		}
		if (MinNeighbor >= MAX_uint16)
		{
			return MAX_uint16; // 四周都还没有任何已知值，暂时无法求解
		}
		return static_cast<uint16>(FMath::Min<uint32>(MinNeighbor + Cost, MAX_uint16 - 1));
	};

	TArray<bool, TInlineAllocator<SECTOR_SIZE * SECTOR_SIZE>> bInActiveList;
	bInActiveList.Init(false, SECTOR_SIZE * SECTOR_SIZE);

	// 对应"if xnb is not in L then ... if p>q then Uxnb<-q; add xnb to L"：
	// 尝试用Loc当前邻居信息重新求一次值，如果比它现在存的值更好就更新并加入OutActiveList
	auto TryAddToActiveList = [&](const FIntVector2& Loc, TArray<FIntVector2>& OutActiveList)
	{
		if (!IsInsideSectorLocalBounds(Loc))
		{
			return;
		}
		const int32 Index = GetLocalGridIndex(Loc);
		if (Sector.CostFields[Index].IsWall())
		{
			return; // 墙，永远不参与积分
		}
		if ((static_cast<uint8>(IntegrationFields[Index].IntegrationFlags) & static_cast<uint8>(FIntegrationField::EIntegratedFlags::LineOfSight)) != 0)
		{
			return; // 已经被LOS Pass固定，属于FIM里的"Source/已收敛点"，不再参与Cost Integration的松弛
		}
		if (bInActiveList[Index])
		{
			return; // 已经在活跃列表里了
		}

		const uint16 Candidate = ComputeCandidate(Loc);
		if (Candidate < IntegrationFields[Index].TotalIntegratedCost)
		{
			IntegrationFields[Index].TotalIntegratedCost = Candidate;
			IntegrationFields[Index].IntegrationFlags = static_cast<FIntegrationField::EIntegratedFlags>(
				static_cast<uint8>(IntegrationFields[Index].IntegrationFlags) | static_cast<uint8>(FIntegrationField::EIntegratedFlags::ActiveWaveFront));
			bInActiveList[Index] = true;
			OutActiveList.Add(Loc);
		}
	};

	TArray<FIntVector2> ActiveList;

	if (Target.IsType<FPortal::FPortalIndex>())
	{
		// Portal Target：种子是PortalBitMap覆盖的整段边界格子，代价直接定为0(相当于FIM里的Source点，
		// 不进活跃列表，只有它的邻居才进)；不跨Sector优化，见设计约定
		const FPortal& Portal = WorldMapData->GetPortal(Target.Get<FPortal::FPortalIndex>());
		const FPortal::EPortalType EdgeType = Portal.GetPortalType();
		const int32 StartIndex = FWorldMapData::GetEdgeGridStartIndex(Sector, EdgeType);
		const int32 IndexStride = (EdgeType == FPortal::EPortalType::Up || EdgeType == FPortal::EPortalType::Down) ? 1 : SECTOR_SIZE;

		TArray<FIntVector2, TInlineAllocator<SECTOR_SIZE>> SeedLocations;
		for (int32 i = 0; i < SECTOR_SIZE; ++i)
		{
			const int32 Bit = SECTOR_SIZE - 1 - i; // 与ProcessSectorPortalsOnEdge的编码约定一致
			if ((Portal.PortalBitMap & (1 << Bit)) == 0)
			{
				continue;
			}
			const int32 GridIndex = StartIndex + i * IndexStride;
			const FIntVector2 Loc(GridIndex % SECTOR_SIZE, GridIndex / SECTOR_SIZE);
			if (Sector.CostFields[GridIndex].IsWall())
			{
				continue; // 墙，不能作为种子
			}
			IntegrationFields[GridIndex].TotalIntegratedCost = 0;
			SeedLocations.Add(Loc);
		}
		// 分两轮：先把所有种子格子的代价都定成0，再统一把它们的邻居拉进活跃列表，
		// 避免"邻居的邻居"引用到还没赋0值的另一个种子格子
		for (const FIntVector2& SeedLoc : SeedLocations)
		{
			for (const FLocalDir& Dir : Directions)
			{
				TryAddToActiveList(FIntVector2(SeedLoc.X + Dir.DX, SeedLoc.Y + Dir.DY), ActiveList);
			}
		}
	}
	else
	{
		// Goal Target：LOS Pass已经把LineOfSight格子的代价算准了（相当于FIM里的Source/Alive点，
		// 本身不参与迭代），把它们的非LOS邻居拉进活跃列表作为起点
		for (int32 Index = 0; Index < IntegrationFields.Num(); ++Index)
		{
			if ((static_cast<uint8>(IntegrationFields[Index].IntegrationFlags) & static_cast<uint8>(FIntegrationField::EIntegratedFlags::LineOfSight)) != 0)
			{
				const FIntVector2 Loc(Index % SECTOR_SIZE, Index / SECTOR_SIZE);
				for (const FLocalDir& Dir : Directions)
				{
					TryAddToActiveList(FIntVector2(Loc.X + Dir.DX, Loc.Y + Dir.DY), ActiveList);
				}
			}
		}
	}

	// FIM Algorithm 3.2主循环：一轮一轮同步处理活跃列表——每个点用当前4邻居重新求解一次，
	// 收敛(代价不再变化)就移出活跃列表并尝试把邻居拉进来；没收敛就留到下一轮继续算
	while (ActiveList.Num() > 0)
	{
		TArray<FIntVector2> NextActiveList;
		for (const FIntVector2& Loc : ActiveList)
		{
			const int32 Index = GetLocalGridIndex(Loc);
			const uint16 OldCost = IntegrationFields[Index].TotalIntegratedCost;
			const uint16 NewCost = ComputeCandidate(Loc);
			IntegrationFields[Index].TotalIntegratedCost = NewCost;

			if (NewCost == OldCost) // 收敛：整数代价场用严格相等作为epsilon=0
			{
				bInActiveList[Index] = false;
				for (const FLocalDir& Dir : Directions)
				{
					TryAddToActiveList(FIntVector2(Loc.X + Dir.DX, Loc.Y + Dir.DY), NextActiveList);
				}
				// 不加回NextActiveList：相当于"remove x from L"
			}
			else
			{
				NextActiveList.Add(Loc); // 还没收敛，下一轮继续处理，bInActiveList保持true
			}
		}
		ActiveList = MoveTemp(NextActiveList);
	}
}

void UPathFinder::ComputeFlowFieldInto(FSector::FSectorID SectorID, const FFlowFieldTarget& Target, FFlowFieldResult& OutResult) const
{
	OutResult.SectorID = SectorID;
	OutResult.Target = Target;

	const FSector& Sector = WorldMapData->GetSectorData(SectorID);
	FIntegrationFieldArray IntegrationFields;

	// 只有Target是Goal格子时才跑LOS Pass并算出GoalLocalGridLocation；Target是Portal时跳过LOS，
	// 直接进入下面的Cost Integration Pass，GoalLocalGridLocation保持(0,0)也不会被用到(没有LOS格子)
	FIntVector2 GoalLocalGridLocation(0, 0);
	if (Target.IsType<FIntVector2>())
	{
		const FIntVector2 SectorLocalOrigin = WorldMapData->GetSectorLocation(SectorID);
		const FIntVector2 GoalGridLocation = Target.Get<FIntVector2>();
		GoalLocalGridLocation = FIntVector2(GoalGridLocation.X - SectorLocalOrigin.X, GoalGridLocation.Y - SectorLocalOrigin.Y);
		RunLOSPass(Sector, GoalLocalGridLocation, IntegrationFields);
	}

	RunCostIntegrationPass(Sector, Target, IntegrationFields);
	RunFlowFieldPass(Sector, IntegrationFields, GoalLocalGridLocation, Target, OutResult);
	
#if WITH_EDITORONLY_DATA
	OutResult.IntegrationCosts.SetNum(IntegrationFields.Num());
	for (int32 i = 0; i < IntegrationFields.Num(); ++i)
	{
		OutResult.IntegrationCosts[i] = IntegrationFields[i].TotalIntegratedCost;
	}
#endif
}

FFlowField::EDirection UPathFinder::QuantizeDirection(float DeltaX, float DeltaY)
{
	if (FMath::IsNearlyZero(DeltaX) && FMath::IsNearlyZero(DeltaY))
	{
		return FFlowField::EDirection::P_0;
	}
	float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(DeltaY, DeltaX));
	if (AngleDeg < 0.f)
	{
		AngleDeg += 360.f;
	}
	const int32 Index = FMath::RoundToInt(AngleDeg / 22.5f) % 16;
	return static_cast<FFlowField::EDirection>(Index);
}

void UPathFinder::RunFlowFieldPass(const FSector& Sector, const FIntegrationFieldArray& IntegrationFields, const FIntVector2& GoalLocalGridLocation, const FFlowFieldTarget& Target, FFlowFieldResult& OutResult) const
{
	OutResult.Directions.SetNum(SECTOR_SIZE * SECTOR_SIZE);
	
	static const FIntVector2 EightDirs[8] = { {1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1} };
	// 每个方向对应的FFlowField::EDirection下标（16方向枚举中的主方向偶数下标）
	static const uint8 EightDirEDirectionIndex[8] = { 0, 4, 8, 12, 2, 6, 10, 14 };
	
	// Portal目标的离开方向（跨扇区法向），用于种子格子
	FFlowField::EDirection ExitDirection = FFlowField::EDirection::P_0;
	const bool bHasPortalTarget = Target.IsType<FPortal::FPortalIndex>();
	if (bHasPortalTarget)
	{
		const FPortal& TargetPortal = WorldMapData->GetPortal(Target.Get<FPortal::FPortalIndex>());
		switch (TargetPortal.GetPortalType())
		{
		case FPortal::EPortalType::Left:  ExitDirection = FFlowField::EDirection::P_180; break;
		case FPortal::EPortalType::Right: ExitDirection = FFlowField::EDirection::P_0;   break;
		case FPortal::EPortalType::Down:  ExitDirection = FFlowField::EDirection::P_270; break;
		case FPortal::EPortalType::Up:    ExitDirection = FFlowField::EDirection::P_90;  break;
		default: break;
		}
	}
	
	for (int32 Y = 0; Y < SECTOR_SIZE; ++Y)
	{
		for (int32 X = 0; X < SECTOR_SIZE; ++X)
		{
			const FIntVector2 Loc(X, Y);
			const int32 Index = GetLocalGridIndex(Loc);
			FFlowField& Field = OutResult.Directions[Index];
			Field = FFlowField();

			if (Sector.CostFields[Index].IsWall() || IntegrationFields[Index].TotalIntegratedCost >= MAX_uint16)
			{
				continue; // 墙，或者积分场没能传播到的死区格子：保持不可通行
			}

			Field.SetPathable();

			const bool bHasLOS = (static_cast<uint8>(IntegrationFields[Index].IntegrationFlags) & static_cast<uint8>(FIntegrationField::EIntegratedFlags::LineOfSight)) != 0;
			if (bHasLOS)
			{
				// LOS格子能直视Goal，直接算连续方向量化，比跟8邻居比大小更精确、也更省一次积分场查表
				Field.SetHasLineOfSight();
				Field.SetDirection(QuantizeDirection(
					static_cast<float>(GoalLocalGridLocation.X - Loc.X),
					static_cast<float>(GoalLocalGridLocation.Y - Loc.Y)));
				continue;
			}

			// 非LOS格子：比较8个邻居的积分代价，朝最便宜的那个走
			uint32 BestCost = IntegrationFields[Index].TotalIntegratedCost;
			int32 BestDirIndex = -1;
			for (int32 DirIndex = 0; DirIndex < 8; ++DirIndex)
			{
				const FIntVector2& Dir = EightDirs[DirIndex];
				const FIntVector2 Neighbor(Loc.X + Dir.X, Loc.Y + Dir.Y);
				if (!IsInsideSectorLocalBounds(Neighbor))
				{
					continue;
				}
				const int32 NeighborIndex = GetLocalGridIndex(Neighbor);
				if (Sector.CostFields[NeighborIndex].IsWall())
				{
					continue; // 墙
				}

				if (Dir.X != 0 && Dir.Y != 0)
				{
					// 对角方向：要求两侧的正交邻居都不是墙，否则视为被墙角挡住，禁止抄近道穿墙角
					const FIntVector2 Flank1(Loc.X + Dir.X, Loc.Y);
					const FIntVector2 Flank2(Loc.X, Loc.Y + Dir.Y);
					if (!IsInsideSectorLocalBounds(Flank1) || !IsInsideSectorLocalBounds(Flank2)
						|| Sector.CostFields[GetLocalGridIndex(Flank1)].IsWall()
						|| Sector.CostFields[GetLocalGridIndex(Flank2)].IsWall())
					{
						continue;
					}
				}

				const uint16 NeighborCost = IntegrationFields[NeighborIndex].TotalIntegratedCost;
				if (NeighborCost < BestCost)
				{
					BestCost = NeighborCost;
					BestDirIndex = DirIndex;
				}
			}

			if (BestDirIndex >= 0)
			{
				Field.SetDirection(static_cast<FFlowField::EDirection>(EightDirEDirectionIndex[BestDirIndex]));
			}
			else if (bHasPortalTarget && IntegrationFields[Index].TotalIntegratedCost == 0)
			{
				// Portal种子格子（扇区出口）：方向=跨扇区法向，把实体推出当前扇区
				Field.SetDirection(ExitDirection);
			}
			// 其余情况方向保留默认P_0，Pathable已置位，实体走到这附近基本等于到达
		}
	}
}

void UPathFinder::RequestFlowFieldsForChain(uint16 SourceSectorRawID, uint16 GoalSectorRawID, const FIntVector2& GoalGridLocation,
	const TArray<FPortal::FPortalIndex>& PortalChain, TArray<TSharedPtr<FFlowFieldResult>>& OutResults)
{
	OutResults.Reset();

	if (WorldMapData == nullptr)
	{
		return;
	}

	const FFlowFieldTarget GoalTarget(TInPlaceType<FIntVector2>(), GoalGridLocation);

	if (PortalChain.Num() == 0)
	{
		// Source和Goal在同一个Sector，直接走向Goal真实格子
		OutResults.Add(GetOrComputeFlowField(GoalSectorRawID, GoalTarget));
		return;
	}

	// 起点段：Source所在Sector -> 链上第一个Portal，跟中间段用同一个函数，无需特殊分支
#if WITH_EDITOR
	{
		const FPortal& P = WorldMapData->GetPortal(PortalChain[0]);
		UE_LOG(LogFlowField, Warning, TEXT("[FFField] S(%d,%d) I%d -> Portal#%d"),
			P.OwingSectorID.RawID % WorldMapData->GetSectorCountX(),
			P.OwingSectorID.RawID / WorldMapData->GetSectorCountX(),
			P.OwingIslandField.IslandId, PortalChain[0].RawIndex);
	}
#endif
	OutResults.Add(GetOrComputeFlowField(SourceSectorRawID, FFlowFieldTarget(TInPlaceType<FPortal::FPortalIndex>(), PortalChain[0])));

	// 中间段：相邻两个Portal若同属一个Sector，说明流场要穿过这个Sector
	for (int32 Index = 0; Index + 1 < PortalChain.Num(); ++Index)
	{
		const FPortal& PortalA = WorldMapData->GetPortal(PortalChain[Index]);
		const FPortal& PortalB = WorldMapData->GetPortal(PortalChain[Index + 1]);
		if (PortalA.OwingSectorID == PortalB.OwingSectorID)
		{
#if WITH_EDITOR
			UE_LOG(LogFlowField, Warning, TEXT("[FFField] S(%d,%d) I%d -> Portal#%d"),
				PortalA.OwingSectorID.RawID % WorldMapData->GetSectorCountX(),
				PortalA.OwingSectorID.RawID / WorldMapData->GetSectorCountX(),
				PortalA.OwingIslandField.IslandId, PortalChain[Index + 1].RawIndex);
#endif
			OutResults.Add(GetOrComputeFlowField(PortalA.OwingSectorID.RawID, FFlowFieldTarget(TInPlaceType<FPortal::FPortalIndex>(), PortalChain[Index + 1])));
		}
	}

	// 终点段：Goal所在Sector -> Goal真实格子；不依赖PortalChain.Last()具体是哪个Portal，
	// 因为Goal段的流场只由(GoalSectorRawID, GoalTarget)决定，从哪个Portal进入不影响方向，天然复用
#if WITH_EDITOR
	UE_LOG(LogFlowField, Warning, TEXT("[FFField] S(%d,%d) -> GoalGrid(%d,%d)"),
		GoalSectorRawID % WorldMapData->GetSectorCountX(),
		GoalSectorRawID / WorldMapData->GetSectorCountX(),
		GoalGridLocation.X, GoalGridLocation.Y);
#endif
	OutResults.Add(GetOrComputeFlowField(GoalSectorRawID, GoalTarget));
}


