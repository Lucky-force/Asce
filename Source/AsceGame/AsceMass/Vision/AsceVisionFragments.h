// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassEntityElementTypes.h"
#include "AsceVisionFragments.generated.h"

/**
 * 视野模块自持的 Fragment。
 *
 * 说明：
 * - 视野半径 / 高度只在这里定义，**不改动** AsceCoreFragments.h 的既有 Team/Height fragment；
 * - 会参与网络复制的字段用 UPROPERTY，客户端做本队视野表现时需要（友方恒可见)；
 * - 上一帧所在格是纯本地字段（不复制）。
 */

/**
 * "视野观察者" Fragment：挂在所有会提供视野的单位（含单位类）上。
 * 服务器用它做队伍视野贡献（flood 的 (R, H_e) 参数），客户端用它做本队视野表现计算。
 */
USTRUCT()
struct FAsceVisionViewFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 视野半径（视野格数，uint16）。典型 20 格量级。 */
	UPROPERTY()
	uint16 SightRadius = 20;

	/** 眼睛离地高度（厘米，uint16）。H_e = 源格有效高度 + 该值 */
	UPROPERTY()
	uint16 EyeHeightCm = 0;
};

// 注意：本文件没有独立的"遮挡 Fragment"。挡视野的只有建筑，而"建筑" = 挂
// FAsceGridFootprintFragment 的实体；遮挡高度直接放在该 fragment 的 OcclusionHeightCm
// （见 AsceFootprintFragment.h），由 UAsceVisionOcclusionObserverProcessor 观察 footprint
// 的 Add/Remove 来事件驱动地写/清遮挡层（架构文档 §3.2，取舍记录见该处）。

/**
 * 单位"上帧贡献状态"（**不复制**）：记录它上次在 FogSubsystem 里登记过的完整贡献参数
 * (所在格 / He / R / 队伍)。FogProcessor 每帧用它 diff：
 *  - 变化了 → 按旧参数下线、新参数上线，再把它刷新成当前值；
 *  - 消亡 → AsceVisionViewRemoveObserverProcessor 在实体销毁前读这里拿旧参数下线，
 *    无需 Fog 侧再存一张"单位 → 记录"的集中表（该表已废弃，见 FogSubsystem 头注释）。
 */
USTRUCT()
struct FAsceVisionFogStateFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 上帧所在视野格的一维索引；-1 = 尚未贡献/不在地图内。 */
	UPROPERTY()
	int32 LastVisionCellIndex = -1;

	/** 上帧贡献的视点高度 H_e。 */
	UPROPERTY()
	uint16 LastHe = 0;

	/** 上帧贡献的视野半径 R。 */
	UPROPERTY()
	uint16 LastR = 0;

	/** 上帧贡献的队伍。 */
	UPROPERTY()
	int32 LastTeamKey = -1;
};
