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
				return AsceCostField::PlainPassableCost; // Unblock 只清地形
			}
		}
	}

	// 无 special 命中 ⇒ 原始地形
	return BaseLandCost[CellIndex];
}

bool FAsceLayeredCostField::FlushCell(uint32 GridIndex, FWorldMapData& WorldMapData)
{
	if (!IsValidIndex(GridIndex) || !WorldMapData.CostFields.IsValidIndex(static_cast<int32>(GridIndex)))
	{
		return false;
	}

	const int32 CellIndex = static_cast<int32>(GridIndex);
	const uint8 NewCost = ComposeCellCost(GridIndex);
	if (WorldMapData.CostFields[CellIndex].Cost == NewCost)
	{
		return false;
	}

	WorldMapData.SetCostField(GridIndex, NewCost);
	return true;
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
