// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "MassEntityView.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "AsceReplicationScalarHandlers.generated.h"

/**
 * 通用的"单个0~1标量"同步数据，血量比例/护盾比例/蓝量比例等都复用这一个结构体，
 * 不用每加一个属性就新写一个ReplicatedXXXData。
 * 注意：一个Agent如果要同步多个标量（比如血量+护盾），就在Agent结构体里放多个这个类型的
 * 具名成员（不能靠这个类型本身区分是血量还是护盾，靠的是外面Policy里指定的accessor）。
 */
USTRUCT()
struct FReplicatedScalarData
{
	GENERATED_BODY()

	void SetValue(const float InValue) { Value = InValue; }
	float GetValue() const { return Value; }

private:
	UPROPERTY(Transient)
	float Value = 0.f;
};

/**
 * 客户端侧的通用标量同步Handler，仿照引擎自带的TMassClientBubbleHealthHandler改写成模板。
 *
 * FieldPolicy需要提供（具体写法见Capabilities/AsceHealthReplication.h里的FAsceHealthRatioPolicy）：
 *   using FragmentType = ...;                                             要读写的Fragment类型
 *   static void ApplyToFragment(FragmentType&, float Value);              把同步值写回Fragment
 *   static float ExtractFromFragment(const FragmentType&);                从Fragment里提取要同步的值
 *   static constexpr float Tolerance;                                     变化超过多少才重新标脏
 */
template<typename AgentArrayItem, typename FieldPolicy>
class TMassClientBubbleScalarHandler
{
public:
	typedef typename FieldPolicy::FragmentType FFragmentType;

	explicit TMassClientBubbleScalarHandler(TClientBubbleHandlerBase<AgentArrayItem>& InOwnerHandler)
		: OwnerHandler(InOwnerHandler)
	{}

#if UE_REPLICATION_COMPILE_CLIENT_CODE
	static void AddRequirementsForSpawnQuery(FMassEntityQuery& InQuery)
	{
		InQuery.AddRequirement<FFragmentType>(EMassFragmentAccess::ReadWrite);
	}

	void CacheFragmentViewsForSpawnQuery(FMassExecutionContext& InExecContext)
	{
		FragmentList = InExecContext.GetMutableFragmentView<FFragmentType>();
	}

	void ClearFragmentViewsForSpawnQuery()
	{
		FragmentList = TArrayView<FFragmentType>();
	}

	void SetSpawnedEntityData(const int32 EntityIdx, const FReplicatedScalarData& Data) const
	{
		FieldPolicy::ApplyToFragment(FragmentList[EntityIdx], Data.GetValue());
	}

	static void SetModifiedEntityData(const FMassEntityView& EntityView, const FReplicatedScalarData& Data)
	{
		FieldPolicy::ApplyToFragment(EntityView.GetFragmentData<FFragmentType>(), Data.GetValue());
	}
#endif // UE_REPLICATION_COMPILE_CLIENT_CODE

protected:
	TArrayView<FFragmentType> FragmentList;
	TClientBubbleHandlerBase<AgentArrayItem>& OwnerHandler;
};

/**
 * 服务器侧（Replicator Processor用）的通用标量同步Handler。
 * ModifyEntity最终会调用BubbleHandlerType::SetScalarFromFragment(...)——现在这个函数
 * 直接实现在通用的TAsceScalarClientBubbleHandler模板里（见Core/AsceScalarReplicationBubbleHandler.h），
 * 因为该模板本身就是TClientBubbleHandlerBase的直接派生类，天然能访问
 * AgentLookupArray/Agents/Serializer这些protected成员，不需要业务方再手写。
 */
template<typename FieldPolicy>
class TMassReplicationProcessorScalarHandler
{
public:
	typedef typename FieldPolicy::FragmentType FFragmentType;

	/**
	 * Presence默认给Any：这个Query最终会被合并进UAsceUnitReplicator共用的那一个EntityQuery上。
	 * 用All会导致"entity必须同时具备所有已注册能力的Fragment才会被同步"；
	 * 用Optional又会导致"一个已注册能力的Fragment都没有的entity也会进这个Query白跑一趟"
	 * （Optional只保证访问安全，不参与Archetype级别的过滤）。
	 * Any的语义是"整个Query范围内，只要命中已标记Any的Fragment中的任意一个就算通过"——
	 * 所有Capability都用Any，等价于"至少命中一个已注册能力"，见AsceReplicationCapability.h。
	 */
	static void AddRequirements(FMassEntityQuery& InQuery, const EMassFragmentPresence Presence = EMassFragmentPresence::Any)
	{
		InQuery.AddRequirement<FFragmentType>(EMassFragmentAccess::ReadOnly, Presence);
	}

	void CacheFragmentViews(FMassExecutionContext& ExecContext)
	{
		FragmentList = ExecContext.GetFragmentView<FFragmentType>();
	}

	void AddEntity(const int32 EntityIdx, FReplicatedScalarData& InOutData) const
	{
		InOutData.SetValue(FieldPolicy::ExtractFromFragment(FragmentList[EntityIdx]));
	}

	template<typename BubbleHandlerType>
	void ModifyEntity(const FMassReplicatedAgentHandle Handle, const int32 EntityIdx, BubbleHandlerType& Bubble) const
	{
		Bubble.SetScalarFromFragment(Handle, FragmentList[EntityIdx]);
	}

protected:
	TConstArrayView<FFragmentType> FragmentList;
};
