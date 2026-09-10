// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AsceVisionDefines.h"

/**
 * 视野模块的高度场（架构文档 §3.1/§3.2/§3.4）。
 *
 * 非反射纯数据结构，由 UAsceFogSubsystem 持有；不参与网络复制（见文档 §3.3：
 * 静态地形双端从同一地图数据本地初始化；动态遮挡由两端各自从本地遮挡实体集合重建）。
 *
 * 布局：
 * - 静态高度场 StaticHeights：uint16 / 格，默认 0（平坦地面占位），留 Set 接口给后续烘焙接入；
 * - 动态遮挡层 DynamicOcclusion：uint16 / 格（与静态同尺寸，值语义稀疏——只有被建筑占的格非 0）。
 *   由遮挡实体 Observer 事件驱动地**局部**写/清：建筑入场 SetDynamicOcclusionRange（写入 footprint）、
 *   退场 ClearDynamicOcclusionRect（清零 footprint；建筑 footprint 互不重叠故无残留）。
 *   变化格（与旧值不同）收集进 OcclusionDirtyCells 供上层做视野翻转失效（文档 §5.5）。
 *   用全尺寸数组而非 TMap 换取 flood/查询的连续内存随机访问；遮挡实体少，局部写只碰 footprint 格，
 *   内存 2~4MB/层 可接受（想再省再换稀疏，见实现 TODO）。
 *
 * 有效视线高度 Hv(cell) = max(StaticHeights[cell], DynamicOcclusion[cell])（文档 §3.4），
 * 由 GetEffectiveHeight 提供，flood 只依赖它，不读任何 cost/block。
 */

class FAsceVisionHeightMap
{
public:
	FAsceVisionHeightMap() = default;

	/** 初始化网格尺寸（此后不变）。GridW/GridH 是视野格数量。 */
	void Init(int32 InGridW, int32 InGridH, float InCellSizeCm = AsceVision::DefaultVisionCellSizeCm);

	bool IsValid() const { return GridW > 0 && GridH > 0; }
	int32 GetGridW() const { return GridW; }
	int32 GetGridH() const { return GridH; }
	int32 GetGridCount() const { return GridW * GridH; }
	float GetCellSizeCm() const { return CellSizeCm; }

	// ── 坐标换算（与 AsceVisionDefines 一致，基于本图自己的 CellSize） ──

	FORCEINLINE FIntVector2 WorldToCell(const FVector2D& WorldPos) const
	{
		return AsceVision::WorldToVisionCell(WorldPos, CellSizeCm);
	}

	FORCEINLINE bool IsInside(const FIntVector2& Cell) const
	{
		return Cell.X >= 0 && Cell.X < GridW && Cell.Y >= 0 && Cell.Y < GridH;
	}

	FORCEINLINE int32 CellToIndex(const FIntVector2& Cell) const
	{
		return Cell.X + Cell.Y * GridW;
	}

	FORCEINLINE FIntVector2 IndexToCell(int32 Index) const
	{
		return FIntVector2(Index % GridW, Index / GridW);
	}

	// ── 静态高度场（地形 + 烘焙静态物，来源待接入，先全 0） ──

	void ResetStaticToZero();

	/** 单格设置（后续烘焙/编辑器导出入口）。Index 越界则忽略。 */
	void SetStaticHeightAt(int32 Index, uint16 Height);
	void SetStaticHeightAtCell(const FIntVector2& Cell, uint16 Height);

	/** 区域/整图批量入口（后续从外部高度数据源灌入用；这里给出整图数组版）。 */
	void SetStaticHeights(TArray<uint16>&& InHeights);

	FORCEINLINE uint16 GetStaticHeight(int32 Index) const
	{
		return StaticHeights.IsValidIndex(Index) ? StaticHeights[Index] : 0;
	}

	// ── 动态遮挡层（稀疏语义，重建驱动） ──

	/** 整层清零（重建第一步：实体销毁/移出后遮挡自然消失，文档 §3.2）。 */
	void ClearDynamicOcclusion();

	/** 重建时写入一块遮挡：footprint 已被调用方换算成视野格范围（§2 整除约束保证无损）。
	 *  X 方向 [InX0, InX1]、Y 方向 [InY0, InY1]（含两端）。与旧值不同则标脏。 */
	void SetDynamicOcclusionRange(int32 InX0, int32 InY0, int32 InX1, int32 InY1, uint16 OcclusionHeight);

	/** 局部清除一块遮挡（建筑拆除/移走，建筑 footprint 互不重叠故直接清零无残留）。
	 *  与旧值不同则标脏（进 OcclusionDirtyCells）。 */
	void ClearDynamicOcclusionRect(int32 InX0, int32 InY0, int32 InX1, int32 InY1);

	/** 上一轮重建（Clear 之后、本次重建写完）相对旧的差异格。用完由上层 ResetOcclusionDirty() 清空。 */
	const TArray<int32>& GetOcclusionDirtyCells() const { return OcclusionDirtyCells; }
	void ResetOcclusionDirty() { OcclusionDirtyCells.Reset(); }

	FORCEINLINE uint16 GetDynamicOcclusion(int32 Index) const
	{
		return DynamicOcclusion.IsValidIndex(Index) ? DynamicOcclusion[Index] : 0;
	}

	/** 动态遮挡层整数组读取（FogSubsystem 用它做"旧值快照 → diff"的精确脏判定，§5.5）。 */
	const TArray<uint16>& GetDynamicOcclusionArray() const { return DynamicOcclusion; }

	// ── 有效视线高度（文档 §3.4：flood 唯一依赖） ──

	/** 有效视线高度 Hv = max(静态, 动态)。Index 越界返回 0（图外当作平视低地，但 flood 不越界扩散）。 */
	FORCEINLINE uint16 GetEffectiveHeight(int32 Index) const
	{
		const uint16 Static = GetStaticHeight(Index);
		const uint16 Dynamic = GetDynamicOcclusion(Index);
		return Static > Dynamic ? Static : Dynamic;
	}

	// 图外按"墙"处理（flood 不越出地图边界），越界高度给一个不可穿越的高值。
	static constexpr uint16 OutOfBoundsHeight = MAX_uint16;

private:
	int32 GridW = 0;
	int32 GridH = 0;
	float CellSizeCm = AsceVision::DefaultVisionCellSizeCm;

	TArray<uint16> StaticHeights;
	TArray<uint16> DynamicOcclusion;
	TArray<int32> OcclusionDirtyCells;
};
