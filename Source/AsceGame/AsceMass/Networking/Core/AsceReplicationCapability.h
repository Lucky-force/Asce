// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

struct FMassEntityQuery;
struct FMassExecutionContext;
struct FMassReplicationContext;

/**
 * "同步能力"插件接口：一个能力 = 一种entity可以自由决定要不要的同步字段（坐标/生命值/护盾……）。
 *
 * 背景（详见架构文档"Capability Bubble拆分"一节）：MassReplication要求一个entity只能绑定
 * 一个ReplicatorClass（UMassReplicationTrait::Params.ReplicatorClass在entity身上是唯一的
 * FMassReplicationParameters常量共享Fragment，加两次第二次会被ensure警告并直接忽略）。
 * 所以真正对外注册给引擎的Replicator只有一个：UAsceUnitReplicator。
 *
 * 这个接口就是让"决策者合一，能力仍然可以自由插拔"成立的关键：每新增一种同步能力，
 * 只需要新写一对Capability文件（实现这个接口）+ 在UAsceReplicationBootstrapSubsystem里
 * 注册一行，不用碰UAsceUnitReplicator本身，也不用碰其他能力的代码。
 *
 * 实现须知：
 *   - AddRequirements里加自己的Fragment需求时，必须传EMassFragmentPresence::Any，
 *     不能用默认的All——因为最终会合并到同一个共享EntityQuery上，用All的话会变成
 *     "entity必须同时具备所有已注册能力的Fragment才会被同步"，这不是我们想要的效果。
 *     也不要用Optional——Optional只保证访问安全，不参与Archetype级别过滤，会导致
 *     "一个已注册能力的Fragment都没有的entity也会进这个Query白跑一趟"。
 *     Any的语义是"整个Query范围内，只要命中已标记Any的Fragment中的任意一个就算通过"，
 *     所有Capability都用Any，正好等价于"至少命中一个已注册能力才处理这批entity"。
 *   - ProcessClientReplication里动手前，必须先用Context.DoesArchetypeHaveFragment<T>()
 *     确认这个chunk里的entity真的有自己需要的Fragment（Any只保证"至少有人命中"，
 *     不保证"每个具体Capability都命中"）。
 */
class FAsceReplicationCapabilityBase
{
public:
	virtual ~FAsceReplicationCapabilityBase() = default;

	/** 对应UMassReplicatorBase::AddRequirements，只管加自己关心的Fragment需求（务必用Any） */
	virtual void AddRequirements(FMassEntityQuery& EntityQuery) = 0;

	/** 对应UMassReplicatorBase::ProcessClientReplication，内部务必先判断DoesArchetypeHaveFragment */
	virtual void ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext) = 0;
};
