// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "MassEntityTypes.h"
#include "AsceFootprintFragment.generated.h"

/**
 * 建筑类Entity的格子占地信息。
 *
 * 设计要点：
 * - 只支持AABB（矩形），不支持任意bitmap形状；
 * - 支持0/90/180/270度旋转——旋转在放置时算一次swap宽高，
 *   不在这里存Rotation枚举，Width/Height在放置那一刻就已经是旋转后的最终值；
 * - 单位(会动的Entity)不挂这个Fragment，不参与CostField写入，
 *   斥力靠运动学层面的排斥处理，不需要动态阻挡格子；
 * - 建筑 footprint 互不重叠（放置规则保证），因此各消费者 Observer 都不需要
 *   维护"重叠建筑"的格子引用计数。
 *
 * 生命周期消费（都观察本 fragment 的 Add/Remove，事件驱动，各模块独立实现）：
 * - 寻路：FlowField/AsceCostFieldFootprintObserverProcessor —— 写入/回滚 CostField 阻挡；
 * - 视野：Vision/AsceVisionOcclusionObserverProcessor —— 写入/清除动态遮挡层（见下方 OcclusionHeightCm）。
 */
USTRUCT(BlueprintType)
struct FAsceGridFootprintFragment : public FMassFragment
{
	GENERATED_BODY()

	// 占地的原点格子坐标（左下角），旋转后的最终坐标系
	UPROPERTY(BlueprintReadWrite)
	FIntPoint OriginGridLocation = FIntPoint::ZeroValue;

	// 占地沿 X 轴占多少格（宽），旋转后的最终值
	UPROPERTY(BlueprintReadWrite)
	int32 SizeX = 1;

	// 占地沿 Y 轴占多少格（纵深），旋转后的最终值
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int32 SizeY = 1;

	// ── 视野遮挡高度（AsceMass/Vision 视野模块共用本 fragment） ──
	// 领域事实：会挡视野的只有建筑，而"建筑"= 挂 FAsceGridFootprintFragment 的实体，
	// 所以把遮挡高直接放这里（0 = 不挡视野），避免为遮挡另设一个生命周期不同步的 fragment
	// （设计取舍记录见视野文档 §3.2，Observer 监听 footprint 的 Add/Remove 即可同时拿到
	// 范围与高度）。单位 cm，世界高度语义，与视野的"有效视线高度 Hv"同基准（max 叠加）。
	UPROPERTY(EditAnywhere)
	uint16 OcclusionHeightCm = 0;
};
