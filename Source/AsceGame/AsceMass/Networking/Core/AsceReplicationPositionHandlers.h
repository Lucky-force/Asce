// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "MassEntityView.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "AsceMass/Fragments/AsceMovementFragments.h"
#include "AsceReplicationPositionHandlers.generated.h"

namespace UE::AsceMass::Replication
{
	// 位置变化超过这个（cm）才重新标脏，跟引擎自带PositionYaw用的容差保持一致
	constexpr float PositionReplicateTolerance = 1.f;

	// 客户端收到网络位置后，跟本地当前位置的偏差分档处理（见FAscePositionCorrectionFragment注释）：
	// <= IgnoreThreshold：忽略，维持本地位置不动；
	// (IgnoreThreshold, SnapThreshold)：记为待修正目标，交给UAsceMovementCorrectionProcessor
	//   在后续帧里匀速追赶；
	// >= SnapThreshold：视为大偏差（复活/传送/长时间掉线重连），直接Snap。
	constexpr float PositionCorrectionIgnoreThreshold = 5.f;
	constexpr float PositionCorrectionSnapThreshold = 200.f;
} // namespace UE::AsceMass::Replication

/**
 * "坐标"独立同步用的payload。之所以要跟速度（见AsceReplicationVelocityHandlers.h）拆成两个
 * 独立的Capability/FastArrayItem，而不是像最早那样合并成一个Position+Velocity的payload：
 *
 * FastArraySerializer是按"item"为粒度做脏标记/网络发送的——一个item只要有任意字段被标脏，
 * 整个item（这里如果合并的话就是位置+速度一起）都会被送一遍。如果位置和速度各自的变化频率/
 * 需要的同步及时性不一样（比如速度想要更快地反映转向变化，位置只在偏差比较大时才需要纠正），
 * 合并payload会导致两者互相拖累：任何一个变化都会把另一个没变的字段也搭车发出去，没法做到
 * "谁超过自己的容差谁就单独触发同步"。拆成两个独立Capability后，各自的FastArrayItem独立判脏，
 * 互不影响，谁变化谁自己发；后续如果还想让两者用不同的LOD更新频率，也是在各自的Capability里
 * 独立调整的基础上进行，不会牵扯到对方。
 */
USTRUCT()
struct FReplicatedAgentPositionData
{
	GENERATED_BODY()

public:
	void SetPosition(const FVector& InPosition) { Position = InPosition; }
	const FVector& GetPosition() const { return Position; }

private:
	UPROPERTY(Transient)
	FVector Position = FVector::ZeroVector;
};

/**
 * 通用"位置"同步的客户端Bubble Handler模板，跟AsceReplicationScalarHandlers.h/
 * AsceScalarReplicationBubbleHandler.h里的TAsceScalarClientBubbleHandler是同一个思路：
 * 直接继承TClientBubbleHandlerBase<AgentArrayItem>，才能访问AgentHandleManager/
 * AgentLookupArray/Agents/Serializer这些protected成员。
 *
 * 要求AgentArrayItem::FReplicatedAgentType（即FAsceXxxReplicatedAgent）提供：
 *   const FReplicatedAgentPositionData& GetReplicatedPositionData() const;
 *   FReplicatedAgentPositionData& GetReplicatedPositionDataMutable();
 *
 * 客户端收到数据后写回Fragment会按偏差大小走"小忽略/中平滑追/大Snap"三档处理，
 * 具体阈值/平滑追赶的Processor见FAscePositionCorrectionFragment
 * （AsceMass/Fragments/AsceMovementFragments.h）和UAsceMovementCorrectionProcessor的注释。
 */
template<typename AgentArrayItem>
class TAscePositionClientBubbleHandler : public TClientBubbleHandlerBase<AgentArrayItem>
{
public:
	using Super = TClientBubbleHandlerBase<AgentArrayItem>;
	using FAgentType = typename AgentArrayItem::FReplicatedAgentType;

#if UE_REPLICATION_COMPILE_SERVER_CODE
	/** 服务器侧：Fragment变化时，判断位置是否超过容差，超过就写进Agent+标脏 */
	void SetPositionFromFragment(const FMassReplicatedAgentHandle Handle, const FTransform& Transform)
	{
		check(this->AgentHandleManager.IsValidHandle(Handle));
		const int32 AgentsIdx = this->AgentLookupArray[Handle.GetIndex()].AgentsIdx;

		AgentArrayItem& Item = (*this->Agents)[AgentsIdx];
		checkf(Item.Agent.GetNetID().IsValid(), TEXT("位置不应该在Agent还没Add的时候更新"));

		FReplicatedAgentPositionData& ReplicatedData = Item.Agent.GetReplicatedPositionDataMutable();
		const FVector NewPosition = Transform.GetLocation();

		if (!NewPosition.Equals(ReplicatedData.GetPosition(), UE::AsceMass::Replication::PositionReplicateTolerance))
		{
			ReplicatedData.SetPosition(NewPosition);
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
			InQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
			InQuery.AddRequirement<FAscePositionCorrectionFragment>(EMassFragmentAccess::ReadWrite);
		};

		auto CacheFragmentViewsForSpawnQuery = [this](FMassExecutionContext& InExecContext)
		{
			TransformList = InExecContext.GetMutableFragmentView<FTransformFragment>();
			CorrectionList = InExecContext.GetMutableFragmentView<FAscePositionCorrectionFragment>();
		};

		auto SetSpawnedEntityData = [this](const FMassEntityView& EntityView, const FAgentType& ReplicatedEntity, const int32 EntityIdx)
		{
			ApplyToFragment(TransformList[EntityIdx], CorrectionList[EntityIdx], ReplicatedEntity.GetReplicatedPositionData());
		};

		auto SetModifiedEntityData = [](const FMassEntityView& EntityView, const FAgentType& Item)
		{
			ApplyToFragment(EntityView.GetFragmentData<FTransformFragment>(), EntityView.GetFragmentData<FAscePositionCorrectionFragment>(), Item.GetReplicatedPositionData());
		};

		this->PostReplicatedAddHelper(AddedIndices, AddRequirementsForSpawnQuery, CacheFragmentViewsForSpawnQuery, SetSpawnedEntityData, SetModifiedEntityData);

		TransformList = TArrayView<FTransformFragment>();
		CorrectionList = TArrayView<FAscePositionCorrectionFragment>();
	}

	virtual void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize) override
	{
		auto SetModifiedEntityData = [](const FMassEntityView& EntityView, const FAgentType& Item)
		{
			ApplyToFragment(EntityView.GetFragmentData<FTransformFragment>(), EntityView.GetFragmentData<FAscePositionCorrectionFragment>(), Item.GetReplicatedPositionData());
		};

		this->PostReplicatedChangeHelper(ChangedIndices, SetModifiedEntityData);
	}

	/**
	 * 写回Fragment的统一入口，见类头注释：偏差很小忽略、很大直接Snap（同时清掉可能还没追完的
	 * 旧修正目标，避免旧目标和新Snap位置互相打架）、中间量级记为待修正目标交给
	 * UAsceMovementCorrectionProcessor在后续帧里平滑追赶。
	 */
	static void ApplyToFragment(FTransformFragment& TransformFragment, FAscePositionCorrectionFragment& CorrectionFragment, const FReplicatedAgentPositionData& Data)
	{
		using namespace UE::AsceMass::Replication;

		const FVector CurrentPosition = TransformFragment.GetTransform().GetLocation();
		const FVector NewPosition = Data.GetPosition();
		const float ErrorDistance = FVector::Dist(CurrentPosition, NewPosition);

		if (ErrorDistance <= PositionCorrectionIgnoreThreshold)
		{
			// 偏差很小，忽略即可——保留本地位置不动，避免为了追平一点点误差反而产生抖动。
			// 顺带清掉可能存在的旧修正目标：既然当前实际位置已经很接近服务器最新值了，
			// 没必要再继续追一个（大概率是更旧的）目标。
			CorrectionFragment.bCorrectionPending = false;
			return;
		}

		if (ErrorDistance >= PositionCorrectionSnapThreshold)
		{
			// 偏差很大（复活/传送/长时间掉线重连之类），平滑追赶反而会造成明显的"滑步"观感，
			// 不如直接Snap更自然。
			TransformFragment.GetMutableTransform().SetLocation(NewPosition);
			CorrectionFragment.bCorrectionPending = false;
			return;
		}

		// 中间量级：不在这里直接改位置，交给UAsceMovementCorrectionProcessor在后续帧里
		// 匀速追赶，画面上表现为一个平滑的修正过程。
		CorrectionFragment.TargetPosition = NewPosition;
		CorrectionFragment.bCorrectionPending = true;
	}
#endif // UE_REPLICATION_COMPILE_CLIENT_CODE

protected:
	TArrayView<FTransformFragment> TransformList;
	TArrayView<FAscePositionCorrectionFragment> CorrectionList;
};

/**
 * 服务器侧（Replicator Processor用）的位置同步Handler，固定读FTransformFragment。
 */
class FAsceReplicationProcessorPositionHandler
{
public:
	static void AddRequirements(FMassEntityQuery& InQuery)
	{
		// Any：这个Query最终会合并进UAsceUnitReplicator共用的EntityQuery，理由见AsceReplicationCapability.h
		InQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Any);
	}

	void CacheFragmentViews(FMassExecutionContext& ExecContext)
	{
		TransformList = ExecContext.GetFragmentView<FTransformFragment>();
	}

	void AddEntity(const int32 EntityIdx, FReplicatedAgentPositionData& InOutData) const
	{
		InOutData.SetPosition(TransformList[EntityIdx].GetTransform().GetLocation());
	}

	template<typename BubbleHandlerType>
	void ModifyEntity(const FMassReplicatedAgentHandle Handle, const int32 EntityIdx, BubbleHandlerType& Bubble) const
	{
		Bubble.SetPositionFromFragment(Handle, TransformList[EntityIdx].GetTransform());
	}

protected:
	TConstArrayView<FTransformFragment> TransformList;
};
