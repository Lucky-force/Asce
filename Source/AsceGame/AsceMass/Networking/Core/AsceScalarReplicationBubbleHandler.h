// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "AsceReplicationScalarHandlers.h"

/**
 * 通用"单个标量属性同步"客户端Bubble Handler模板，跟服务器侧的TAsceScalarReplicationCapability
 * 是配套的一对。新增一个标量属性能力时，Bubble Handler这一层也不用再手写了，直接：
 *
 *   using FAsceShieldClientBubbleHandler = TAsceScalarClientBubbleHandler<FAsceShieldFastArrayItem, FAsceShieldRatioPolicy>;
 *
 * 之所以以前（比如最早的AsceHealthReplication.h）要把SetScalarFromFragment/PostReplicatedAdd/
 * PostReplicatedChange手写在具体的Bubble Handler子类上，是因为TClientBubbleHandlerBase的
 * AgentHandleManager/AgentLookupArray/Agents/Serializer都是protected成员，只有它的直接派生类
 * 才能访问——这个模板本身就是TClientBubbleHandlerBase<AgentArrayItem>的直接派生类（模板参数
 * 不影响继承关系），所以能把这些逻辑收进模板里统一实现，不需要业务方每次重写。
 *
 * 注意：这个类不是USTRUCT/UCLASS，不需要反射，可以放心模板化——但AgentArrayItem
 * （FAsceXxxFastArrayItem）本身仍然必须是手写的USTRUCT，见文件头对应的说明。
 */
template<typename AgentArrayItem, typename FieldPolicy>
class TAsceScalarClientBubbleHandler : public TClientBubbleHandlerBase<AgentArrayItem>
{
public:
	using Super = TClientBubbleHandlerBase<AgentArrayItem>;
	using FScalarHandler = TMassClientBubbleScalarHandler<AgentArrayItem, FieldPolicy>;
	using FAgentType = typename AgentArrayItem::FReplicatedAgentType;

	TAsceScalarClientBubbleHandler()
		: ScalarHandler(*this)
	{}

#if UE_REPLICATION_COMPILE_SERVER_CODE
	/** 服务器侧：Fragment变化时，判断是否超过Tolerance，超过就写进Agent+标脏 */
	void SetScalarFromFragment(const FMassReplicatedAgentHandle Handle, const typename FieldPolicy::FragmentType& Fragment)
	{
		check(this->AgentHandleManager.IsValidHandle(Handle));
		const int32 AgentsIdx = this->AgentLookupArray[Handle.GetIndex()].AgentsIdx;

		AgentArrayItem& Item = (*this->Agents)[AgentsIdx];
		checkf(Item.Agent.GetNetID().IsValid(), TEXT("标量属性不应该在Agent还没Add的时候更新"));

		FReplicatedScalarData& ReplicatedData = FieldPolicy::GetMutableData(Item.Agent);
		const float NewValue = FieldPolicy::ExtractFromFragment(Fragment);

		if (!FMath::IsNearlyEqual(NewValue, ReplicatedData.GetValue(), FieldPolicy::Tolerance))
		{
			ReplicatedData.SetValue(NewValue);
			this->Serializer->MarkItemDirty(Item);
		}
	}
#endif // UE_REPLICATION_COMPILE_SERVER_CODE

protected:
#if UE_REPLICATION_COMPILE_CLIENT_CODE
	virtual void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize) override
	{
		auto AddRequirementsForSpawnQuery = [](FMassEntityQuery& InQuery)
		{
			FScalarHandler::AddRequirementsForSpawnQuery(InQuery);
		};

		auto CacheFragmentViewsForSpawnQuery = [this](FMassExecutionContext& InExecContext)
		{
			ScalarHandler.CacheFragmentViewsForSpawnQuery(InExecContext);
		};

		auto SetSpawnedEntityData = [this](const FMassEntityView& EntityView, const FAgentType& ReplicatedEntity, const int32 EntityIdx)
		{
			ScalarHandler.SetSpawnedEntityData(EntityIdx, FieldPolicy::GetData(ReplicatedEntity));
		};

		auto SetModifiedEntityData = [](const FMassEntityView& EntityView, const FAgentType& Item)
		{
			FScalarHandler::SetModifiedEntityData(EntityView, FieldPolicy::GetData(Item));
		};

		this->PostReplicatedAddHelper(AddedIndices, AddRequirementsForSpawnQuery, CacheFragmentViewsForSpawnQuery, SetSpawnedEntityData, SetModifiedEntityData);

		ScalarHandler.ClearFragmentViewsForSpawnQuery();
	}

	virtual void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize) override
	{
		auto SetModifiedEntityData = [](const FMassEntityView& EntityView, const FAgentType& Item)
		{
			FScalarHandler::SetModifiedEntityData(EntityView, FieldPolicy::GetData(Item));
		};

		this->PostReplicatedChangeHelper(ChangedIndices, SetModifiedEntityData);
	}
#endif // UE_REPLICATION_COMPILE_CLIENT_CODE

	FScalarHandler ScalarHandler;
};
