// AsceCostFieldChunkActor.cpp
#include "AsceCostFieldChunkActor.h"

#include "Net/UnrealNetwork.h"
#include "AsceMass/FlowField/AscePathFindingSubsystem.h"
#include "AsceMass/FlowField/AsceWorldMapReplicatorActor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceCostFieldChunkActor)

// ────────────────────────────────────────────
// FAsceCostFieldChunkData（FastArray 回调）
// ────────────────────────────────────────────

void FAsceCostFieldChunkData::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize)
{
}

void FAsceCostFieldChunkData::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize)
{
	if (!OwnerActor)
	{
		return;
	}
	for (const int32 PackIndex : AddedIndices)
	{
		OwnerActor->MarkPackDirty(PackIndex);
	}
}

void FAsceCostFieldChunkData::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize)
{
	if (!OwnerActor)
	{
		return;
	}
	for (const int32 PackIndex : ChangedIndices)
	{
		OwnerActor->MarkPackDirty(PackIndex);
	}
}

// ────────────────────────────────────────────
// Actor
// ────────────────────────────────────────────

AAsceCostFieldChunkActor::AAsceCostFieldChunkActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	// 客户端 Tick 里应用待处理的 pack（服务端没有脏包，空转开销极小）
	PrimaryActorTick.bCanEverTick = true;

	// 构造先于属性反序列化 ⇒ 回调里能安全拿到 OwnerActor
	ChunkData.OwnerActor = this;
}

void AAsceCostFieldChunkActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAsceCostFieldChunkActor, ChunkData);
	DOREPLIFETIME(AAsceCostFieldChunkActor, StartCellIndex);
	DOREPLIFETIME(AAsceCostFieldChunkActor, CellCount);
}

void AAsceCostFieldChunkActor::ConfigureFromWorld(int32 InStartCellIndex, int32 InCellCount, const FWorldMapData& WorldMapData)
{
	StartCellIndex = InStartCellIndex;
	CellCount = InCellCount;

	const int32 PackCount = FMath::DivideAndRoundUp(InCellCount, FAsceCostFieldPack::CellsPerPack);
	ChunkData.Packs.Reset();
	ChunkData.Packs.SetNum(PackCount);

	for (int32 PackIndex = 0; PackIndex < PackCount; ++PackIndex)
	{
		FAsceCostFieldPack& Pack = ChunkData.Packs[PackIndex];
		for (int32 Slot = 0; Slot < FAsceCostFieldPack::CellsPerPack; ++Slot)
		{
			const int32 LocalCell = PackIndex * FAsceCostFieldPack::CellsPerPack + Slot;
			if (LocalCell >= InCellCount)
			{
				break;   // 尾包空槽保持 0
			}
			const int32 GlobalIndex = InStartCellIndex + LocalCell;
			if (WorldMapData.CostFields.IsValidIndex(GlobalIndex))
			{
				Pack.SetCell(Slot, WorldMapData.CostFields[GlobalIndex].Cost);
			}
		}
		ChunkData.MarkItemDirty(Pack);
	}
}

void AAsceCostFieldChunkActor::SetCellCostFromWorld(uint32 GridIndex, uint8 Cost)
{
	const int32 LocalCell = static_cast<int32>(GridIndex) - StartCellIndex;
	if (LocalCell < 0 || LocalCell >= CellCount)
	{
		return;
	}

	const int32 PackIndex = LocalCell / FAsceCostFieldPack::CellsPerPack;
	const int32 Slot = LocalCell % FAsceCostFieldPack::CellsPerPack;
	if (!ChunkData.Packs.IsValidIndex(PackIndex))
	{
		return;
	}

	FAsceCostFieldPack& Pack = ChunkData.Packs[PackIndex];
	if (Pack.GetCell(Slot) == Cost)
	{
		return;   // 值没变：不用标脏（避免包内其它格抖动误发）
	}
	Pack.SetCell(Slot, Cost);
	ChunkData.MarkItemDirty(Pack);
}

void AAsceCostFieldChunkActor::MarkPackDirty(int32 PackIndex)
{
	DirtyPacks.Add(PackIndex);
}

void AAsceCostFieldChunkActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (DirtyPacks.Num() > 0)
	{
		ApplyDirtyPacks();
	}
}

FWorldMapData* AAsceCostFieldChunkActor::ResolveWorldMap() const
{
	UAscePathFindingSubsystem* PathFinding = UAscePathFindingSubsystem::Get(this);
	AAsceWorldMapReplicatorActor* Replicator = PathFinding ? PathFinding->GetWorldMapReplicator() : nullptr;
	return Replicator ? &Replicator->WorldMapData : nullptr;
}

void AAsceCostFieldChunkActor::ApplyDirtyPacks()
{
	// world init 未完成时先留着，下一帧再试（本地结构没就绪就不能应用）
	FWorldMapData* World = ResolveWorldMap();
	if (!World)
	{
		return;
	}

	for (const int32 PackIndex : DirtyPacks)
	{
		if (!ChunkData.Packs.IsValidIndex(PackIndex))
		{
			continue;
		}
		const FAsceCostFieldPack& Pack = ChunkData.Packs[PackIndex];

		for (int32 Slot = 0; Slot < FAsceCostFieldPack::CellsPerPack; ++Slot)
		{
			const int32 LocalCell = PackIndex * FAsceCostFieldPack::CellsPerPack + Slot;
			if (LocalCell >= CellCount)
			{
				break;
			}
			const int32 GlobalIndex = StartCellIndex + LocalCell;
			if (!World->CostFields.IsValidIndex(GlobalIndex))
			{
				continue;
			}

			// 以本地 dense 为 diff 基准：只把真变了的格推进处理链
			const uint8 NewCost = Pack.GetCell(Slot);
			if (World->CostFields[GlobalIndex].Cost != NewCost)
			{
				World->ApplyReplicatedCostCell(static_cast<uint32>(GlobalIndex), NewCost);
			}
		}
	}

	DirtyPacks.Reset();
}
