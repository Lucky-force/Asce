// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "MassEntityTypes.h"
#include "AsceFootprintFragment.generated.h"

/**
 * 建筑类Entity的格子占地信息。
 *
 * 设计要点（详见架构文档）：
 * - 只支持AABB（矩形），不支持任意bitmap形状；
 * - 支持0/90/180/270度旋转——旋转在放置时算一次swap宽高，
 *   不在这里存Rotation枚举，Width/Height在放置那一刻就已经是旋转后的最终值；
 * - 单位(会动的Entity)不挂这个Fragment，不参与CostField写入，
 *   斥力靠运动学层面的排斥处理，不需要动态阻挡格子。
 *
 * TODO(下一轮): 
 * 1. Add/Remove Observer Processor，往CostField里写入/回滚阻挡，
 *    需要一个全局格子引用计数（Key=全局格子坐标，不绑定Sector）处理重叠建筑；
 * 2. 这个Observer需要依赖运行时的寻路Subsystem（UPathFinder+FWorldMapData），
 *    这个Subsystem这一轮还没搭，所以这里先只定义数据结构。
 */
USTRUCT(BlueprintType)
struct FAsceGridFootprintFragment : public FMassFragment
{
	GENERATED_BODY()

	// 占地的原点格子坐标（左下角），旋转后的最终坐标系
	UPROPERTY(BlueprintReadWrite)
	FIntPoint OriginGridLocation = FIntPoint::ZeroValue;

	// 占地宽度（格子数），旋转后的最终值
	UPROPERTY(BlueprintReadWrite)
	int32 Width = 1;

	// 占地高度（格子数），旋转后的最终值
	UPROPERTY(BlueprintReadWrite)
	int32 Height = 1;
};
