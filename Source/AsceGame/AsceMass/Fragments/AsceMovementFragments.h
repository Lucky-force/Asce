// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassEntityTypes.h"
#include "AsceMovementFragments.generated.h"

/**
 * 是否要对这个entity跑"完整本地模拟"（流场采样+RVO，重新计算Velocity）的开关。
 *
 * 用bool字段而不是Tag：Tag挂/摘会导致entity在Archetype间搬迁（内存重新排布），
 * 战争迷雾/摄像机可见性这种判定频繁变化的场景下开销不small；bool只是普通字段读写，
 * 不影响Archetype，改起来没有额外代价。
 *
 * 由战争迷雾系统（后续开发）负责写这个字段（判定"这个entity值不值得让本地跑一遍完整寻路+避障"），
 * 目前占位先默认true（等于"所有entity都跑完整模拟"），战争迷雾接入后换掉写入方即可，
 * 读这个字段的AsceMovementSteeringProcessor不需要跟着改。
 *
 * true：AsceMovementSteeringProcessor这一帧会真的查流场方向+跑RVO，重新算出DesiredVelocity。
 * false：跳过上面这一步，DesiredVelocity维持"当前值不变"——对于被网络同步覆盖的entity，
 * 这个"当前值"就是服务器最后一次同步下来的速度，配合引擎自带的UMassApplyMovementProcessor
 * (Velocity=DesiredVelocity，再对Transform位置做积分)，天然实现了Dead Reckoning外推，
 * 不需要为Dead Reckoning再单独写一个Processor。
 */
USTRUCT()
struct FAsceMovementSimStateFragment : public FMassFragment
{
	GENERATED_BODY()

	bool bRunFullSimulation = true;

	// ── 到达状态（见 UAsceArrivalProcessor）──
	/** true = 当前不在"移动订单执行中"（无订单 / 已到达）；false = 正在执行移动订单。
	 *  默认 true（没有寻路=已到位）；ApplyMergedMoveOrder 写入订单时置 false；
	 *  到达（路径1：自己走到目标点 / 路径2：与同组已到达单位接触）时置 true；
	 *  被顶飞（距离超过 plan 的到达判定半径）回退寻路时置 false；订单被拒绝/取消时置 true。 */
	bool bArrived = true;
};

/**
 * 客户端专用：网络位置误差校正的"待修正目标"状态，由UAsceMovementCorrectionProcessor消费。
 *
 * 背景：Client收到服务器权威位置（见AsceReplicationPositionHandlers.h）时，跟本地当前位置
 * 之间通常存在偏差（本地预测误差、Dead Reckoning外推误差等），直接Snap会造成明显的画面跳变。
 * 按偏差大小分三档处理：
 *   - 很小（<= PositionCorrectionIgnoreThreshold）：忽略，维持本地位置不动，避免抖动；
 *   - 中等：不立即改位置，而是把服务器位置记到这里，交给UAsceMovementCorrectionProcessor
 *     在后续若干帧里匀速追上（FMath::VInterpConstantTo），画面上表现为一个平滑的"追赶"；
 *   - 很大（>= PositionCorrectionSnapThreshold，比如复活/传送/长时间掉线重连）：
 *     直接Snap，因为这种量级的偏差平滑追赶反而会造成"滑步"观感，不如直接跳变自然。
 * 具体阈值见AsceReplicationPositionHandlers.h里的PositionCorrectionIgnoreThreshold/
 * PositionCorrectionSnapThreshold。
 *
 * 只在bCanMove的entity上挂（配合FAsceMovementSimStateFragment），静态建筑不需要这个。
 */
USTRUCT()
struct FAscePositionCorrectionFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 服务器权威位置，正在被匀速追赶的目标；bCorrectionPending=false时这个字段值无意义 */
	FVector TargetPosition = FVector::ZeroVector;

	/** 是否有一个尚未追平的中等偏差修正在进行中 */
	bool bCorrectionPending = false;
};
