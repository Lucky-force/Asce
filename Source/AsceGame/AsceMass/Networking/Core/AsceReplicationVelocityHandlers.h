// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "MassEntityView.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "AsceReplicationVelocityHandlers.generated.h"

namespace UE::AsceMass::Replication
{
	// 速度变化超过这个（cm/s）才重新标脏，避免转向/避障产生的小幅抖动导致高频同步
	constexpr float VelocityReplicateTolerance = 10.f;

	// 速度量化用的缩放系数：用int16（而不是FVector三个double，UE5默认FVector是double即
	// 8字节/分量）存放"速度*10"取整后的结果，内存/带宽从24字节（3*8）降到6字节（3*int16），
	// 省了75%。系数选10而不是100，是因为int16范围是±32767，/10还原后能表示±3276.7cm/s，
	// 相比当前MaxSpeed默认值300cm/s留了10倍余量，即使以后策划调高速度或者加冲刺/击退之类
	// 临时加速也不容易超出范围被截断；用100的话只能到±327.67cm/s，跟300太接近，余量太小。
	// 精度是0.1cm/s，对于几百cm/s量级的速度完全够用。
	constexpr float VelocityQuantizeScale = 10.f;

	FORCEINLINE int16 QuantizeVelocityComponent(const float InValue)
	{
		return static_cast<int16>(FMath::Clamp(FMath::RoundToInt(InValue * VelocityQuantizeScale), int32(MIN_int16), int32(MAX_int16)));
	}

	FORCEINLINE float DequantizeVelocityComponent(const int16 InValue)
	{
		return static_cast<float>(InValue) / VelocityQuantizeScale;
	}
} // namespace UE::AsceMass::Replication

/**
 * "速度"独立同步用的payload。跟位置（见AsceReplicationPositionHandlers.h）拆成两个独立
 * Capability/FastArrayItem的理由见那边的类头注释——核心是让"谁超过自己的容差谁就单独触发
 * 同步"成立，不再被合并payload的另一个字段拖累一起发。
 *
 * 用int16量化存储（*10取整/使用时/10还原），而不是原始FVector三个double分量，
 * 单个Agent的速度部分从24字节（3*8）降到6字节（3*int16），省了75%，
 * 具体系数选取理由见VelocityQuantizeScale的注释。
 */
USTRUCT()
struct FReplicatedAgentVelocityData
{
	GENERATED_BODY()

public:
	void SetVelocity(const FVector& InVelocity)
	{
		using namespace UE::AsceMass::Replication;
		VelocityX = QuantizeVelocityComponent(InVelocity.X);
		VelocityY = QuantizeVelocityComponent(InVelocity.Y);
		VelocityZ = QuantizeVelocityComponent(InVelocity.Z);
	}

	FVector GetVelocity() const
	{
		using namespace UE::AsceMass::Replication;
		return FVector(DequantizeVelocityComponent(VelocityX), DequantizeVelocityComponent(VelocityY), DequantizeVelocityComponent(VelocityZ));
	}

private:
	UPROPERTY(Transient)
	int16 VelocityX = 0;

	UPROPERTY(Transient)
	int16 VelocityY = 0;

	UPROPERTY(Transient)
	int16 VelocityZ = 0;
};

/**
 * 通用"速度"同步的客户端Bubble Handler模板，写法跟TAscePositionClientBubbleHandler
 * （AsceReplicationPositionHandlers.h）是同一个思路，只是写回的Fragment/字段不同：
 * 这里写的是FMassDesiredMovementFragment.DesiredVelocity，而不是FTransformFragment的位置。
 *
 * 要求AgentArrayItem::FReplicatedAgentType（即FAsceXxxReplicatedAgent）提供：
 *   const FReplicatedAgentVelocityData& GetReplicatedVelocityData() const;
 *   FReplicatedAgentVelocityData& GetReplicatedVelocityDataMutable();
 */
template<typename AgentArrayItem>
class TAsceVelocityClientBubbleHandler : public TClientBubbleHandlerBase<AgentArrayItem>
{
public:
	using Super = TClientBubbleHandlerBase<AgentArrayItem>;
	using FAgentType = typename AgentArrayItem::FReplicatedAgentType;

#if UE_REPLICATION_COMPILE_SERVER_CODE
	/** 服务器侧：Fragment变化时，判断速度是否超过容差，超过就写进Agent+标脏 */
	void SetVelocityFromFragment(const FMassReplicatedAgentHandle Handle, const FVector& Velocity)
	{
		check(this->AgentHandleManager.IsValidHandle(Handle));
		const int32 AgentsIdx = this->AgentLookupArray[Handle.GetIndex()].AgentsIdx;

		AgentArrayItem& Item = (*this->Agents)[AgentsIdx];
		checkf(Item.Agent.GetNetID().IsValid(), TEXT("速度不应该在Agent还没Add的时候更新"));

		FReplicatedAgentVelocityData& ReplicatedData = Item.Agent.GetReplicatedVelocityDataMutable();

		if (!Velocity.Equals(ReplicatedData.GetVelocity(), UE::AsceMass::Replication::VelocityReplicateTolerance))
		{
			ReplicatedData.SetVelocity(Velocity);
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
			InQuery.AddRequirement<FMassDesiredMovementFragment>(EMassFragmentAccess::ReadWrite);
		};

		auto CacheFragmentViewsForSpawnQuery = [this](FMassExecutionContext& InExecContext)
		{
			DesiredMovementList = InExecContext.GetMutableFragmentView<FMassDesiredMovementFragment>();
		};

		auto SetSpawnedEntityData = [this](const FMassEntityView& EntityView, const FAgentType& ReplicatedEntity, const int32 EntityIdx)
		{
			ApplyToFragment(DesiredMovementList[EntityIdx], ReplicatedEntity.GetReplicatedVelocityData());
		};

		auto SetModifiedEntityData = [](const FMassEntityView& EntityView, const FAgentType& Item)
		{
			ApplyToFragment(EntityView.GetFragmentData<FMassDesiredMovementFragment>(), Item.GetReplicatedVelocityData());
		};

		this->PostReplicatedAddHelper(AddedIndices, AddRequirementsForSpawnQuery, CacheFragmentViewsForSpawnQuery, SetSpawnedEntityData, SetModifiedEntityData);

		DesiredMovementList = TArrayView<FMassDesiredMovementFragment>();
	}

	virtual void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize) override
	{
		auto SetModifiedEntityData = [](const FMassEntityView& EntityView, const FAgentType& Item)
		{
			ApplyToFragment(EntityView.GetFragmentData<FMassDesiredMovementFragment>(), Item.GetReplicatedVelocityData());
		};

		this->PostReplicatedChangeHelper(ChangedIndices, SetModifiedEntityData);
	}

	/**
	 * 写回Fragment的统一入口：直接Snap赋值给DesiredVelocity，不做平滑。
	 * 跟位置不一样（见AsceReplicationPositionHandlers.h的小忽略/中平滑/大Snap三档），
	 * 速度本身的变化不会造成视觉上的"跳变"——它只是改变了后续几帧的加速度/朝向，
	 * 由引擎自带的UMassApplyMovementProcessor每帧积分位置时自然而然地体现出来，
	 * 不需要额外的平滑处理。
	 */
	static void ApplyToFragment(FMassDesiredMovementFragment& DesiredMovementFragment, const FReplicatedAgentVelocityData& Data)
	{
		DesiredMovementFragment.DesiredVelocity = Data.GetVelocity();
	}
#endif // UE_REPLICATION_COMPILE_CLIENT_CODE

protected:
	TArrayView<FMassDesiredMovementFragment> DesiredMovementList;
};

/**
 * 服务器侧（Replicator Processor用）的速度同步Handler，固定读FMassDesiredMovementFragment。
 */
class FAsceReplicationProcessorVelocityHandler
{
public:
	static void AddRequirements(FMassEntityQuery& InQuery)
	{
		// Any：这个Query最终会合并进UAsceUnitReplicator共用的EntityQuery，理由见AsceReplicationCapability.h
		InQuery.AddRequirement<FMassDesiredMovementFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Any);
	}

	void CacheFragmentViews(FMassExecutionContext& ExecContext)
	{
		DesiredMovementList = ExecContext.GetFragmentView<FMassDesiredMovementFragment>();
	}

	void AddEntity(const int32 EntityIdx, FReplicatedAgentVelocityData& InOutData) const
	{
		InOutData.SetVelocity(DesiredMovementList[EntityIdx].DesiredVelocity);
	}

	template<typename BubbleHandlerType>
	void ModifyEntity(const FMassReplicatedAgentHandle Handle, const int32 EntityIdx, BubbleHandlerType& Bubble) const
	{
		Bubble.SetVelocityFromFragment(Handle, DesiredMovementList[EntityIdx].DesiredVelocity);
	}

protected:
	TConstArrayView<FMassDesiredMovementFragment> DesiredMovementList;
};
