// AsceLayeredCostField.h
#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "AsceMass/FlowField/AsceFlowFieldCores.h"

/**
 * 代价来源分层（BaseLand / Building / Special）。
 *
 * 背景：FWorldMapData::CostFields 是"最终结果"，任何一方直接写它都会互相覆盖——
 * 典型问题：建筑拆除时把地形原本的墙/地形代价一并抹成"可通行"。
 * 这里把代价拆成三个来源层，最终值由 `ComposeCellCost` 组合后，只在"与当前 CostFields 不同"时
 * 才写回 `SetCostField`（⇒ 才触发复制 + 扇区/墙簇重建）。
 *
 * - BaseLandCost：地形烘焙基线（bake 应用完抓一次快照）。**保留完整 cost 值**（含 bit7 墙位、
 *   低 7 位地形代价），规范统一、方便以后扩展地形代价（寻路目前只管 block，代价是预留语义）。
 * - BuildingCost：建筑占用层。0 = 无建筑，`AsceCostField::BlockedCost` = 阻挡；
 *   建筑不允许重叠 ⇒ 单值即可，不做多层叠加。
 * - Special：技能/机制施加的临时影响（填海/搭桥等），**只存 Op 不存代价**（代价已确认只影响寻路，
 *   而寻路只管 block）：
 *     · Block   = 强制阻挡
 *     · Unblock = 强制可通行，**只清除地形(BaseLand)的阻挡**，不能清除建筑阻挡（建筑是实体占位，
 *                 清掉会和"实体仍占着这一格"矛盾）
 *   组合规则：`FinalBlocked = BuildingBlocked || AnyBlock || (BaseBlocked && !AnyUnblock)`，
 *   即 Block 优先于 Unblock。
 *
 * Special 的存储（按"访问开销"选型，不是内存）：
 * - `SpecialContributionBitMap`（1 bit/格，全图 dense）：绝大多数格没有 special ⇒ 一次位测试即短路，
 *   完全不碰 TMap；只有真有 special 的格才付一次 find。
 * - `SpecialContributions`（稀疏 TMap，按 GridIndex）：每格 `CostOps` 按 ID 升序，二分插入/删除/查找。
 *
 * Handle 编码：`(GridIndex << 32) | ID` ⇒ 由 Handle 直接得到格子（无需反查表）。
 * 由此有契约：**同一格上同一 ID 只能对应一种 Op**；ID 建议全局单调递增、在一次贡献的生命周期内唯一，
 * 否则陈旧 Handle 可能误删后来复用同一 ID 的贡献。
 *
 * 线程/网络契约：只在**服务端游戏线程**读写（不需要锁）；客户端不建层，只用复制下来的最终 CostFields。
 */
enum class EAsceCostOp : uint8
{
	/** 强制阻挡 */
	Block = 0,
	/** 强制可通行（只清地形阻挡，不清建筑） */
	Unblock = 1,
};

struct FAsceCostOp
{
	EAsceCostOp Op = EAsceCostOp::Block;
	uint32 ID = 0;
};

struct FAsceSpecialCostCell
{
	/** 按 ID 升序（ID 在同一格上唯一 ⇒ 无并列，二分精确） */
	TArray<FAsceCostOp> CostOps;
};

struct FAsceLayeredCostField
{
public:
	static constexpr uint64 InvalidHandle = MAX_uint64;

	ASCE_INLINE static uint64 MakeHandle(uint32 GridIndex, uint32 ID)
	{
		return (static_cast<uint64>(GridIndex) << 32) | static_cast<uint64>(ID);
	}

	// ── 结构 ──

	/** 建层（尺寸 = 流场逻辑格总数）。BaseLand 默认全"纯可通行"，Building 默认 0（无建筑）。 */
	void InitLayeredCostField(uint32 InGridCountX, uint32 InGridCountY);

	ASCE_INLINE bool IsValidIndex(uint32 GridIndex) const
	{
		return GridIndex < static_cast<uint32>(BaseLandCost.Num());
	}

	ASCE_INLINE uint32 GetGridCountX() const { return GridCountX; }
	ASCE_INLINE uint32 GetGridCountY() const { return GridCountY; }
	ASCE_INLINE uint32 GetGridCount() const { return static_cast<uint32>(BaseLandCost.Num()); }

	// ── 层写入（只改层，不动 CostFields；写回由 FlushCell/FlushAll 统一做） ──

	void SetBaseCost(uint32 GridIndex, uint8 NewCost);
	void SetBuildingCost(uint32 GridIndex, uint8 NewCost);

	// ── Special 层 ──

	/** 在某格加一条贡献，返回用于释放的 Handle（越界返回 InvalidHandle，并报 Warning）。 */
	uint64 AddSpecialOp(uint32 GridIndex, EAsceCostOp Op, uint32 ID);

	/** 释放由 AddSpecialOp 返回的 Handle（格子内最后一个贡献移除时同步清 bitmap）。 */
	void RemoveSpecialOp(uint64 Handle);

	// ── 组合 / 写回 ──

	/** 组合某格的最终 cost（越界按阻挡，与 FWorldMapData::IsCellBlocked 一致）。 */
	uint8 ComposeCellCost(uint32 GridIndex) const;

	/** 组合某格并与 WorldMapData 现值比较，**不同才** 写回；返回是否真的改了。 */
	bool FlushCell(uint32 GridIndex, FWorldMapData& WorldMapData);

	/** 全量组合写回（bake 基线建立后用一次）；返回是否有任何格变了。 */
	bool FlushAll(FWorldMapData& WorldMapData);

	// ── 查询/调试 ──

	ASCE_INLINE uint8 GetBaseCost(uint32 GridIndex) const
	{
		return IsValidIndex(GridIndex) ? BaseLandCost[static_cast<int32>(GridIndex)] : AsceCostField::BlockedCost;
	}

	ASCE_INLINE uint8 GetBuildingCost(uint32 GridIndex) const
	{
		return IsValidIndex(GridIndex) ? BuildingCost[static_cast<int32>(GridIndex)] : 0;
	}

	ASCE_INLINE bool HasSpecialContribution(uint32 GridIndex) const
	{
		return IsValidIndex(GridIndex) && static_cast<bool>(SpecialContributionBitMap[static_cast<int32>(GridIndex)]);
	}

	/** 稀疏条目/条目总数（调试用） */
	ASCE_INLINE int32 GetSpecialCellCount() const { return SpecialContributions.Num(); }
	int32 GetSpecialOpCount() const;

private:
	/** 地形烘焙基线：完整 cost 值（含 bit7 墙位） */
	TArray<uint8> BaseLandCost;

	/** 建筑占用：0 = 无，`AsceCostField::BlockedCost` = 阻挡 */
	TArray<uint8> BuildingCost;

	/** 1 bit/格：该格是否存在 special 贡献（dense，用作 TMap 的 O(1) 短路） */
	TBitArray<> SpecialContributionBitMap;

	/** 稀疏 special 贡献；不变式：有 key 的格 ⇔ bitmap 置位 */
	TMap<uint32, FAsceSpecialCostCell> SpecialContributions;

	uint32 GridCountX = 0;
	uint32 GridCountY = 0;
};
