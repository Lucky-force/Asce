// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AsceReplicationCapability.h"
#include "AsceReplicationValueHandlers.h"
#include "MassReplicationProcessor.h"
#include "MassExecutionContext.h"

/**
 * 通用"值同步"能力模板（数据层：Core/AsceReplicationValueHandlers.h），支持一个 Agent 多字段。
 * 绝大多数新增能力（生命/蓝量、护盾、阵营、技能 CD……）跟这套的逻辑骨架完全一样，
 * 唯一区别只是 FieldPolicy 指定的 Fragment/字段/载荷/比较方式，所以不用每次重写
 * AddRequirements/ProcessChunk——直接：
 *
 *   using FAsceShieldReplicationCapability = TAsceValueReplicationCapability<FAsceShieldPolicy>;
 *
 * FieldPolicy 需要提供（详见 AsceReplicationValueHandlers.h）：
 *   FragmentType / ValueType / FValueData / NumFields / GetMutableData / GetData /
 *   ExtractFromFragment / ApplyToFragment / ValuesDiffer +
 *   FAgentType / FFastArrayItemType / FBubbleInfoType 三个类型别名。
 *
 * 不适用的情况（一次要同步 Vector 这类结构，比如 Transform），继续手写继承
 * `FAsceReplicationCapabilityBase`，参考 AsceTransformReplicationCapability.h/.cpp。
 */
template<typename FieldPolicy>
class TAsceValueReplicationCapability : public FAsceReplicationCapabilityBase
{
public:
	using FFragmentType = typename FieldPolicy::FragmentType;
	using FAgentType = typename FieldPolicy::FAgentType;
	using FFastArrayItemType = typename FieldPolicy::FFastArrayItemType;
	using FBubbleInfoType = typename FieldPolicy::FBubbleInfoType;

	virtual void AddRequirements(FMassEntityQuery& EntityQuery) override
	{
		// 默认参数已经是 Any，见 AsceReplicationValueHandlers.h
		TMassReplicationProcessorValueHandler<FieldPolicy>::AddRequirements(EntityQuery);
	}

	virtual void ProcessChunk(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext
		, const FMassClientHandle& ClientHandle, TArrayView<FMassReplicatedAgentHandle> InOutMarkers) override
	{
#if UE_REPLICATION_COMPILE_SERVER_CODE

		// 整个 Query 只保证"这批 entity 至少命中一个已注册能力"，具体到这个 chunk 有没有
		// FFragmentType 还是得自己查一遍
		if (!Context.DoesArchetypeHaveFragment<FFragmentType>())
		{
			return;
		}

		// 本能力自己的 Bubble 实例：**必须按类型从 UMassReplicationSubsystem 查**。
		// 不能用 shared fragment 的 BubbleInfos[client] ——那里只装了该组 params 里"代表 BubbleInfoClass"的那一个实例，
		// 拿它 cast 成别的类会失败（Epic 的模型是"一组实体一个 bubble"，项目按属性拆了多个 bubble）。
		UMassReplicationSubsystem& RepSubsystem = ReplicationContext.ReplicationSubsystem;
		const FMassBubbleInfoClassHandle BubbleClassHandle =
			RepSubsystem.GetBubbleInfoClassHandle(FBubbleInfoType::StaticClass());
		if (!BubbleClassHandle.IsValid())
		{
			// 配置错误：BubbleInfoClass 没在 bootstrap 里注册（继续走会命中引擎 check 崩）
			ensureMsgf(false, TEXT("[Replication] BubbleInfoClass 未注册：%s"), *FBubbleInfoType::StaticClass()->GetName());
			return;
		}

		auto& Bubble = RepSubsystem.GetTypedClientBubbleChecked<FBubbleInfoType>(BubbleClassHandle, ClientHandle)
			->GetAsceSerializer().Bubble;

		TMassReplicationProcessorValueHandler<FieldPolicy> Handler;
		Handler.CacheFragmentViews(Context);

		const TConstArrayView<FMassNetworkIDFragment> NetworkIDList = Context.GetFragmentView<FMassNetworkIDFragment>();
		const TConstArrayView<FReplicationTemplateIDFragment> TemplateIDList = Context.GetFragmentView<FReplicationTemplateIDFragment>();
		const TConstArrayView<FMassReplicationLODFragment> LODList = Context.GetFragmentView<FMassReplicationLODFragment>();

		const int32 NumEntities = Context.GetNumEntities();
		for (int32 EntityIdx = 0; EntityIdx < NumEntities; ++EntityIdx)
		{
			// 是否已经在**本能力自己的bubble**里：查自己handler的NetID表，
			// 不能看引擎共用的 AgentData.Handle（那是所有能力共享的单槽）
			const FMassReplicatedAgentHandle ExistingHandle = Bubble.FindAgentHandleByNetID(NetworkIDList[EntityIdx].NetID);

			if (LODList[EntityIdx].LOD < EMassLOD::Off)
			{
				if (!ExistingHandle.IsValid())
				{
					FAgentType ReplicatedAgent;
					ReplicatedAgent.SetNetID(NetworkIDList[EntityIdx].NetID);
					ReplicatedAgent.SetTemplateID(TemplateIDList[EntityIdx].ID);
					Handler.AddEntity(EntityIdx, ReplicatedAgent);

					const FMassReplicatedAgentHandle NewHandle = Bubble.AddAgent(Context.GetEntity(EntityIdx), ReplicatedAgent);
					if (!InOutMarkers[EntityIdx].IsValid())
					{
						InOutMarkers[EntityIdx] = NewHandle;
					}
				}
				else
				{
					Handler.ModifyEntity(ExistingHandle, EntityIdx, Bubble);
					if (!InOutMarkers[EntityIdx].IsValid())
					{
						InOutMarkers[EntityIdx] = ExistingHandle;
					}
				}
			}
			else if (ExistingHandle.IsValid())
			{
				Bubble.RemoveAgent(ExistingHandle);
			}
		}
#endif // UE_REPLICATION_COMPILE_SERVER_CODE
	}
};
