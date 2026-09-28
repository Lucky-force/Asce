// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassReplicationProcessor.h"
#include "AsceReplicationCapability.h"
#include "AsceUnitReplicator.generated.h"

/**
 * 唯一对外注册给引擎的Replicator（entity的UMassReplicationTrait::Params.ReplicatorClass
 * 统一填这个类，不要再填具体的某个能力）。
 *
 * 内部不写死任何具体字段的同步逻辑，只是把AddRequirements转发给所有已注册能力，并在
 * ProcessClientReplication里对所有能力做**单趟驱动**（每个entity每client恰好处理一次）——
 * 真正的坐标/生命值同步逻辑都在各自的AsceXxxReplicationCapability.h/.cpp里，
 * 新增能力时只需要新写一对文件+注册一行，不需要修改这个类本身。
 *
 * 为什么是单趟驱动，而不是"每个能力各调一次引擎的CalculateClientReplication"：
 * 引擎的FMassReplicatedAgentFragment::AgentData.Handle每个entity只有一个（per client的
 * scratch槽），而项目每个能力有自己的bubble和自己的handle。各调一次的话第二个能力会读到
 * 第一个能力写进去的handle，Modify时把别人的handle传进自己的handler，触发
 * TIndexedHandleManager::IsValidHandle（ManagerID不匹配）断言；而且能力2..N永远不会真正
 * 走到Add分支。详细分析见 ProcessClientReplication 实现里的注释。
 *
 * 复制组参数由 `UAsceRTSEntityTrait`（bReplicateEntity，默认开）自动写入 entity 模板：
 * `ReplicatorClass=UAsceUnitReplicator` + 一个代表 `BubbleInfoClass`（各能力按类型自查自己的 bubble）。
 * **模板不要再手动加引擎的 "Replication" trait**（同 params 加两次第二次会被忽略/ensure）。
 */
UCLASS()
class UAsceUnitReplicator : public UMassReplicatorBase
{
	GENERATED_BODY()

public:
	/** 在AsceReplicationBootstrapSubsystem::PostInitialize（或更早）里，每个能力调一次这个函数 */
	static void RegisterCapability(TUniquePtr<FAsceReplicationCapabilityBase> Capability);

	// ── 视野复制过滤的 client→TeamKey 注册表（**可选覆盖**）──
	// 复制决策（ProcessClientReplication 开头）按"当前 client 的队伍可见集"过滤实体。
	// 默认**不需要**调用：引擎给每个 client 的每个 bubble 类 spawn 的实例 Owner 就是对应 PlayerController，
	// replicator 会自动从它解析 TeamId 并打包成 TeamKey（和 Fog 投影同口径，见 ResolveClientTeamKey）。
	// 这个注册表只作为手动覆盖/测试用；未注册且解析不出来的 client 跳过过滤（不裁剪的安全默认）。
	static void SetClientTeam(int32 ClientIndex, int32 TeamKey);
	static void ClearAllClientTeams();
	
	virtual void AddRequirements(FMassEntityQuery& EntityQuery) override;
	virtual void ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext) override;

private:
	static TArray<TUniquePtr<FAsceReplicationCapabilityBase>>& GetCapabilities();
};
