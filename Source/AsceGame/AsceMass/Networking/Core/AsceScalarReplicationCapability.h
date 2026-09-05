// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AsceReplicationCapability.h"
#include "AsceReplicationScalarHandlers.h"
#include "AsceUnitReplicator.h"
#include "MassExecutionContext.h"

/**
 * 通用"单个标量属性同步"能力模板。绝大多数新增能力（护盾比例/蓝量比例……）跟Health的
 * 逻辑骨架完全一样，唯一的区别只是FieldPolicy指定的类型/存取方式，所以没必要每次都
 * 重写一遍AddRequirements/ProcessClientReplication——直接：
 *
 *   using FAsceShieldReplicationCapability = TAsceScalarReplicationCapability<FAsceShieldRatioPolicy>;
 *
 * 就完事了，具体例子看AsceHealthReplicationCapability.h。
 *
 * FieldPolicy除了AsceReplicationScalarHandlers.h要求的那几个成员（FragmentType/
 * ApplyToFragment/ExtractFromFragment/Tolerance/GetMutableData/GetData），还要多提供三个类型：
 *   using FAgentType         = ...;  对应的FReplicatedAgentBase子类（如FAsceHealthReplicatedAgent）
 *   using FFastArrayItemType = ...;  对应的FMassFastArrayItemBase子类（如FAsceHealthFastArrayItem）
 *   using FBubbleInfoType    = ...;  对应的AMassClientBubbleInfoBase子类（如AAsceHealthClientBubbleInfo）
 *
 * 约定：FBubbleInfoType必须提供GetAsceSerializer()方法，返回内含public Bubble成员的
 * Serializer（照抄现有的AAsceHealthClientBubbleInfo/AAsceTransformClientBubbleInfo写法）。
 *
 * 不适用的情况（比如Transform/Velocity一次要同步位置/速度这种Vector而非标量的字段），
 * 继续手写继承FAsceReplicationCapabilityBase，参考AsceTransformReplicationCapability.h/.cpp
 * 和通用的AsceReplicationPositionHandlers.h/AsceReplicationVelocityHandlers.h。
 */
template<typename FieldPolicy>
class TAsceScalarReplicationCapability : public FAsceReplicationCapabilityBase
{
public:
	using FFragmentType = typename FieldPolicy::FragmentType;
	using FAgentType = typename FieldPolicy::FAgentType;
	using FFastArrayItemType = typename FieldPolicy::FFastArrayItemType;
	using FBubbleInfoType = typename FieldPolicy::FBubbleInfoType;

	virtual void AddRequirements(FMassEntityQuery& EntityQuery) override
	{
		// 默认参数已经是Any，见AsceReplicationScalarHandlers.h
		TMassReplicationProcessorScalarHandler<FieldPolicy>::AddRequirements(EntityQuery);
	}

	virtual void ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext) override
	{
#if UE_REPLICATION_COMPILE_SERVER_CODE

		// 整个Query只保证"这批entity至少命中一个已注册能力"，具体到这个chunk有没有
		// FFragmentType还是得自己查一遍
		if (!Context.DoesArchetypeHaveFragment<FFragmentType>())
		{
			return;
		}

		TMassReplicationProcessorScalarHandler<FieldPolicy> Handler;
		FMassReplicationSharedFragment* RepSharedFrag = nullptr;

		auto CacheViewsCallback = [&RepSharedFrag, &Handler](FMassExecutionContext& Context)
		{
			Handler.CacheFragmentViews(Context);
			RepSharedFrag = &Context.GetMutableSharedFragment<FMassReplicationSharedFragment>();
			check(RepSharedFrag);
		};

		auto AddEntityCallback = [&RepSharedFrag, &Handler](FMassExecutionContext& Context, const int32 EntityIdx, FAgentType& InReplicatedAgent, const FMassClientHandle ClientHandle) -> FMassReplicatedAgentHandle
		{
			FBubbleInfoType& BubbleInfo = RepSharedFrag->GetTypedClientBubbleInfoChecked<FBubbleInfoType>(ClientHandle);

			Handler.AddEntity(EntityIdx, FieldPolicy::GetMutableData(InReplicatedAgent));

			return BubbleInfo.GetAsceSerializer().Bubble.AddAgent(Context.GetEntity(EntityIdx), InReplicatedAgent);
		};

		auto ModifyEntityCallback = [&RepSharedFrag, &Handler](FMassExecutionContext& Context, const int32 EntityIdx, const EMassLOD::Type LOD, const double Time, const FMassReplicatedAgentHandle Handle, const FMassClientHandle ClientHandle)
		{
			FBubbleInfoType& BubbleInfo = RepSharedFrag->GetTypedClientBubbleInfoChecked<FBubbleInfoType>(ClientHandle);
			auto& Bubble = BubbleInfo.GetAsceSerializer().Bubble;

			Handler.ModifyEntity(Handle, EntityIdx, Bubble);
		};

		auto RemoveEntityCallback = [&RepSharedFrag](FMassExecutionContext& Context, const FMassReplicatedAgentHandle Handle, const FMassClientHandle ClientHandle)
		{
			FBubbleInfoType& BubbleInfo = RepSharedFrag->GetTypedClientBubbleInfoChecked<FBubbleInfoType>(ClientHandle);
			BubbleInfo.GetAsceSerializer().Bubble.RemoveAgentChecked(Handle);
		};

		UAsceUnitReplicator::CallCalculateClientReplication<FFastArrayItemType>(Context, ReplicationContext, CacheViewsCallback, AddEntityCallback, ModifyEntityCallback, RemoveEntityCallback);
#endif // UE_REPLICATION_COMPILE_SERVER_CODE
	}
};
