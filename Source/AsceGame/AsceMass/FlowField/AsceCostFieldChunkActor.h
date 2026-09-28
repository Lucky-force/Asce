// AsceCostFieldChunkActor.h
#pragma once

#include "GameFramework/Info.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "AsceMass/FlowField/AsceFlowFieldCores.h"
#include "AsceCostFieldChunkActor.generated.h"

class AAsceCostFieldChunkActor;

/**
 * 代价场分块复制：**一条打包 16 格**（2×uint64，每格 1 字节）。
 *
 * 为什么要打包：FastArray item 的固定开销是 12B（基类）+ 线上每条 8B（FIdxIDPair），
 * "一格一条"会把 1B 的数据放大到 ~13B；16 格/条后约 1.75B/格，单条变更重发 16B（可接受，
 * 建筑 footprint 是连续格、包内自然合并）。
 *
 * 线上尺寸：每条 = 8B(FIdxIDPair) + 16B(payload) = 24B ⇒ 2048 条/块的初发约 48KB < 64KB 上限。
 */
USTRUCT()
struct FAsceCostFieldPack : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	uint64 PackedLo = 0;   // 槽 0..7

	UPROPERTY()
	uint64 PackedHi = 0;   // 槽 8..15

	static constexpr int32 CellsPerPack = 16;

	ASCE_INLINE uint8 GetCell(int32 Slot) const
	{
		return Slot < 8
			? static_cast<uint8>((PackedLo >> (Slot * 8)) & 0xFF)
			: static_cast<uint8>((PackedHi >> ((Slot - 8) * 8)) & 0xFF);
	}

	ASCE_INLINE void SetCell(int32 Slot, uint8 Cost)
	{
		if (Slot < 8)
		{
			const uint64 Mask = static_cast<uint64>(0xFF) << (Slot * 8);
			PackedLo = (PackedLo & ~Mask) | (static_cast<uint64>(Cost) << (Slot * 8));
		}
		else
		{
			const int32 Shift = (Slot - 8) * 8;
			const uint64 Mask = static_cast<uint64>(0xFF) << Shift;
			PackedHi = (PackedHi & ~Mask) | (static_cast<uint64>(Cost) << Shift);
		}
	}
};

/** 块数据（FastArray）：**只装数组**。带 NetDeltaSerializer 的 struct 在 RepLayout 里没有子属性复制
 *  （RepLayout.cpp 注释："NetDeltaSerialize: These structs will not have Child Rep Commands"），
 *  `FastArrayDeltaSerialize` 也只处理 Packs 数组 ⇒ 起始格号/格数这类标量必须放到 Actor 上做普通复制字段
 *  （见 AAsceCostFieldChunkActor::StartCellIndex / CellCount），否则客户端永远是 0、ApplyDirtyPacks 一格都不应用。 */
USTRUCT()
struct FAsceCostFieldChunkData : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FAsceCostFieldPack> Packs;

	/** 回指本 actor（构造时回填；非复制成员，客户端构造时同样会设上） */
	AAsceCostFieldChunkActor* OwnerActor = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FAsceCostFieldPack, FAsceCostFieldChunkData>(Packs, DeltaParms, *this);
	}

	void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize);
	void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
};

template<>
struct TStructOpsTypeTraits<FAsceCostFieldChunkData> : public TStructOpsTypeTraitsBase2<FAsceCostFieldChunkData>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};

/**
 * 代价场分块复制的复制单元：服务器按"每块 ≤ 2048 条（=32768 格）"动态切块 spawn；
 * 每块独立 actor 复制 ⇒ 单块初发 < 64KB，且多个块会被引擎按带宽预算自动摊到多帧（借引擎的分帧），
 * 中途加入的客户端逐块初发即可拿到全图。
 *
 * 服务器：`ConfigureFromWorld` 预填 + `SetCellCostFromWorld` 改格（由世界地图 actor 的 drain 路由）
 * 客户端：pack 回调只记脏，Tick 里和本地 dense diff 后经 `ApplyReplicatedCostCell` 走现有处理链
 */
UCLASS()
class AAsceCostFieldChunkActor : public AInfo
{
	GENERATED_BODY()

public:
	AAsceCostFieldChunkActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** 服务器：从本地 dense 一次性预填本块（spawn 后立刻调用；纯本地，不产生网络洪峰） */
	void ConfigureFromWorld(int32 InStartCellIndex, int32 InCellCount, const FWorldMapData& WorldMapData);

	/** 服务器：改一格并标脏所属包（值从本地 dense 传入；同包多次标脏由 FastArray 合并发送） */
	void SetCellCostFromWorld(uint32 GridIndex, uint8 Cost);

	/** 客户端：某个 pack 变更（由 FAsceCostFieldChunkData 的回调调用；只记脏，Tick 统一应用） */
	void MarkPackDirty(int32 PackIndex);

	UPROPERTY(Replicated)
	FAsceCostFieldChunkData ChunkData;

	/** 本块第一格的全局格号。客户端按 pack 下标 + slot 还原全局格号。
	 *  **必须放 Actor 上**：FastArray 结构体内部的普通 UPROPERTY 不参与复制（见 FAsceCostFieldChunkData 注释）。 */
	UPROPERTY(Replicated)
	int32 StartCellIndex = 0;

	/** 本块格数（= Packs.Num()*16 的裁尾值，尾包空槽为 0）。理由同 StartCellIndex。 */
	UPROPERTY(Replicated)
	int32 CellCount = 0;

private:
	/** 客户端：把待应用 pack 与本地 dense diff，只把真变了的格推进 `PendingUpdateCostFields` */
	void ApplyDirtyPacks();

	FWorldMapData* ResolveWorldMap() const;

	/** 客户端待应用 pack 下标（本地状态，不复制） */
	TSet<int32> DirtyPacks;
};
