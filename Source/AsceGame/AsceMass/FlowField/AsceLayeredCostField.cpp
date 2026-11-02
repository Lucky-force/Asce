// AsceLayeredCostField.cpp
#include "AsceLayeredCostField.h"

#include "Algo/BinarySearch.h"

void FAsceLayeredCostField::InitLayeredCostField(uint32 InGridCountX, uint32 InGridCountY)
{
	GridCountX = InGridCountX;
	GridCountY = InGridCountY;

	const int32 GridNum = static_cast<int32>(InGridCountX * InGridCountY);
	BaseLandCost.Init(AsceCostField::PlainPassableCost, GridNum);
	BuildingCost.Init(0, GridNum);
	ProximityCost.Init(0, GridNum);
	SpecialContributionBitMap.Init(false, GridNum);
	SpecialContributions.Reset();
}

void FAsceLayeredCostField::SetBaseCost(uint32 GridIndex, uint8 NewCost)
{
	if (IsValidIndex(GridIndex))
	{
		BaseLandCost[static_cast<int32>(GridIndex)] = NewCost;
	}
}

void FAsceLayeredCostField::SetBuildingCost(uint32 GridIndex, uint8 NewCost)
{
	if (IsValidIndex(GridIndex))
	{
		BuildingCost[static_cast<int32>(GridIndex)] = NewCost;
	}
}

uint64 FAsceLayeredCostField::AddSpecialOp(uint32 GridIndex, EAsceCostOp Op, uint32 ID)
{
	if (!IsValidIndex(GridIndex))
	{
		UE_LOG(LogFlowField, Warning, TEXT("[LayeredCostField] AddSpecialOp 越界：GridIndex=%u"), GridIndex);
		return InvalidHandle;
	}

	FAsceSpecialCostCell& SpecialCell = SpecialContributions.FindOrAdd(GridIndex);

	const int32 Slot = Algo::LowerBoundBy(SpecialCell.CostOps, ID, &FAsceCostOp::ID);
	if (SpecialCell.CostOps.IsValidIndex(Slot) && SpecialCell.CostOps[Slot].ID == ID)
	{
		// 契约：同一格上同一 ID 只能对应一种 Op（Handle = GridIndex|ID 才能唯一定位）
		if (SpecialCell.CostOps[Slot].Op != Op)
		{
			UE_LOG(LogFlowField, Warning,
				TEXT("[LayeredCostField] 同格同 ID 重复贡献，覆盖旧 Op：GridIndex=%u ID=%u"), GridIndex, ID);
		}
		SpecialCell.CostOps[Slot].Op = Op;
	}
	else
	{
		SpecialCell.CostOps.Insert({Op, ID}, Slot);
	}

	SpecialContributionBitMap[static_cast<int32>(GridIndex)] = true;
	return MakeHandle(GridIndex, ID);
}

void FAsceLayeredCostField::RemoveSpecialOp(uint64 Handle)
{
	if (Handle == InvalidHandle)
	{
		return;
	}

	const uint32 GridIndex = static_cast<uint32>(Handle >> 32);
	const uint32 ID = static_cast<uint32>(Handle);
	if (!IsValidIndex(GridIndex))
	{
		UE_LOG(LogFlowField, Warning, TEXT("[LayeredCostField] RemoveSpecialOp 越界：GridIndex=%u"), GridIndex);
		return;
	}

	FAsceSpecialCostCell* SpecialCell = SpecialContributions.Find(GridIndex);
	if (!SpecialCell)
	{
		// 陈旧/重复释放：直接忽略（不崩，也不动 bitmap）
		UE_LOG(LogFlowField, Warning,
			TEXT("[LayeredCostField] RemoveSpecialOp 找不到条目（陈旧或重复释放）：GridIndex=%u ID=%u"), GridIndex, ID);
		return;
	}

	const int32 Slot = Algo::LowerBoundBy(SpecialCell->CostOps, ID, &FAsceCostOp::ID);
	if (!SpecialCell->CostOps.IsValidIndex(Slot) || SpecialCell->CostOps[Slot].ID != ID)
	{
		UE_LOG(LogFlowField, Warning, TEXT("[LayeredCostField] RemoveSpecialOp 找不到 ID：GridIndex=%u ID=%u"), GridIndex, ID);
		return;
	}

	SpecialCell->CostOps.RemoveAt(Slot, 1, EAllowShrinking::No);
	if (SpecialCell->CostOps.Num() == 0)
	{
		// 不变式：有 key 的格 ⇔ bitmap 置位
		SpecialContributions.Remove(GridIndex);
		SpecialContributionBitMap[static_cast<int32>(GridIndex)] = false;
	}
}

uint8 FAsceLayeredCostField::ComposeCellCost(uint32 GridIndex) const
{
	// 越界按阻挡（与 FWorldMapData::IsCellBlocked 一致）
	if (!IsValidIndex(GridIndex))
	{
		return AsceCostField::BlockedCost;
	}

	const int32 CellIndex = static_cast<int32>(GridIndex);

	// 建筑阻挡不可被 Special 清除
	if (BuildingCost[CellIndex] == AsceCostField::BlockedCost)
	{
		return AsceCostField::BlockedCost;
	}

	uint8 BaseCost = BaseLandCost[CellIndex];

	// Special：bitmap 短路（绝大多数格没有 special，一次位测试即返回）
	if (SpecialContributionBitMap[CellIndex])
	{
		if (const FAsceSpecialCostCell* SpecialCell = SpecialContributions.Find(GridIndex))
		{
			bool bAnyBlock = false;
			bool bAnyUnblock = false;
			for (const FAsceCostOp& CostOp : SpecialCell->CostOps)
			{
				if (CostOp.Op == EAsceCostOp::Block)
				{
					bAnyBlock = true;
					break; // Block 优先，已可判定
				}
				if (CostOp.Op == EAsceCostOp::Unblock)
				{
					bAnyUnblock = true;
				}
			}

			if (bAnyBlock)
			{
				return AsceCostField::BlockedCost;
			}
			if (bAnyUnblock)
			{
				BaseCost = AsceCostField::PlainPassableCost; // Unblock 只清地形
			}
		}
	}

	// 地形墙：原值返回（保留旧版墙的 push direction），墙格自身不吃贴墙代价
	if ((BaseCost & 0b10000000) != 0)
	{
		return BaseCost;
	}

	// 贴墙代价层叠加（多个 block 来源精确累加），clamp 到可通行上限
	const int32 FinalCost = static_cast<int32>(BaseCost) + static_cast<int32>(ProximityCost[CellIndex]);
	return static_cast<uint8>(FMath::Clamp(FinalCost, 1, static_cast<int32>(MaxPassableCost)));
}

void FAsceLayeredCostField::AddProximityAround(const uint32 GridIndex, const int32 Sign)
{
	if (!IsValidIndex(GridIndex) || GridCountX == 0 || GridCountY == 0)
	{
		return;
	}

	const int32 CenterX = static_cast<int32>(GridIndex % GridCountX);
	const int32 CenterY = static_cast<int32>(GridIndex / GridCountX);
	const int32 Radius = ProximityRadius;

	for (int32 DY = -Radius; DY <= Radius; ++DY)
	{
		const int32 NY = CenterY + DY;
		if (NY < 0 || NY >= static_cast<int32>(GridCountY))
		{
			continue;
		}
		for (int32 DX = -Radius; DX <= Radius; ++DX)
		{
			const int32 NX = CenterX + DX;
			if (NX < 0 || NX >= static_cast<int32>(GridCountX))
			{
				continue;
			}
			// Chebyshev 距离（方形环）：紧邻的 8 格每个 block 给 +1（多来源叠加）
			const int32 Distance = FMath::Max(FMath::Abs(DX), FMath::Abs(DY));
			if (Distance == 0 || Distance > ProximityRadius)
			{
				continue; // 自身是墙格，不吃贴墙代价
			}

			const int32 NeighborIndex = NX + NY * static_cast<int32>(GridCountX);
			const int32 Value = static_cast<int32>(ProximityCost[NeighborIndex]) + Sign * ProximityBonus;
			ProximityCost[NeighborIndex] = static_cast<uint16>(FMath::Max(Value, 0));
		}
	}
}

bool FAsceLayeredCostField::FlushNeighborhood(const uint32 GridIndex, FWorldMapData& WorldMapData)
{
	bool bChanged = false;
	if (!IsValidIndex(GridIndex) || GridCountX == 0 || GridCountY == 0)
	{
		return bChanged;
	}

	const int32 CenterX = static_cast<int32>(GridIndex % GridCountX);
	const int32 CenterY = static_cast<int32>(GridIndex / GridCountX);
	const int32 Radius = ProximityRadius;

	for (int32 DY = -Radius; DY <= Radius; ++DY)
	{
		const int32 NY = CenterY + DY;
		if (NY < 0 || NY >= static_cast<int32>(GridCountY))
		{
			continue;
		}
		for (int32 DX = -Radius; DX <= Radius; ++DX)
		{
			if (DX == 0 && DY == 0)
			{
				continue;
			}
			const int32 NX = CenterX + DX;
			if (NX < 0 || NX >= static_cast<int32>(GridCountX))
			{
				continue;
			}
			bChanged |= FlushCell(static_cast<uint32>(NX + NY * static_cast<int32>(GridCountX)), WorldMapData);
		}
	}
	return bChanged;
}

void FAsceLayeredCostField::RebuildProximityCost(const FWorldMapData& WorldMapData)
{
	ProximityCost.Init(0, BaseLandCost.Num());

	const int32 GridNum = FMath::Min(BaseLandCost.Num(), WorldMapData.CostFields.Num());
	for (int32 GridIndex = 0; GridIndex < GridNum; ++GridIndex)
	{
		if (WorldMapData.CostFields[GridIndex].IsWall())
		{
			AddProximityAround(static_cast<uint32>(GridIndex), +1);
		}
	}
}

bool FAsceLayeredCostField::FlushCell(uint32 GridIndex, FWorldMapData& WorldMapData)
{
	if (!IsValidIndex(GridIndex) || !WorldMapData.CostFields.IsValidIndex(static_cast<int32>(GridIndex)))
	{
		return false;
	}

	const int32 CellIndex = static_cast<int32>(GridIndex);
	const bool bWasBlocked = WorldMapData.CostFields[CellIndex].IsWall();
	const uint8 NewCost = ComposeCellCost(GridIndex);
	const bool bNowBlocked = (NewCost & 0b10000000) != 0;

	bool bChanged = false;

	// 阻挡状态变化 ⇒ 维护贴墙代价层（比自身贡献不含自身，所以 NewCost 在加减前后都有效）
	if (bWasBlocked != bNowBlocked)
	{
		AddProximityAround(GridIndex, bNowBlocked ? +1 : -1);
	}

	if (WorldMapData.CostFields[CellIndex].Cost != NewCost)
	{
		WorldMapData.SetCostField(GridIndex, NewCost);
		bChanged = true;
	}

	// 周围 R 格的最终代价变了：逐个写回（它们的阻挡状态没变，不会递归）
	if (bWasBlocked != bNowBlocked)
	{
		bChanged |= FlushNeighborhood(GridIndex, WorldMapData);
	}

	return bChanged;
}

bool FAsceLayeredCostField::FlushAll(FWorldMapData& WorldMapData)
{
	bool bChanged = false;

	const int32 GridNum = FMath::Min(static_cast<int32>(GetGridCount()), WorldMapData.CostFields.Num());
	for (int32 GridIndex = 0; GridIndex < GridNum; ++GridIndex)
	{
		const uint8 NewCost = ComposeCellCost(static_cast<uint32>(GridIndex));
		if (WorldMapData.CostFields[GridIndex].Cost != NewCost)
		{
			WorldMapData.SetCostField(static_cast<uint32>(GridIndex), NewCost);
			bChanged = true;
		}
	}

	return bChanged;
}

int32 FAsceLayeredCostField::GetSpecialOpCount() const
{
	int32 Count = 0;
	for (const TPair<uint32, FAsceSpecialCostCell>& Pair : SpecialContributions)
	{
		Count += Pair.Value.CostOps.Num();
	}
	return Count;
}
