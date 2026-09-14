// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceOrderChannelComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/NumericLimits.h"
#include "MassEntitySubsystem.h"
#include "MassCommonFragments.h"
#include "MassReplicationFragments.h"
#include "AsceMass/Networking/AsceNetIDRegistrySubsystem.h"
#include "AsceMass/FlowField/AscePathFindingSubsystem.h"
#include "AsceMass/FlowField/AscePathFinder.h"
#include "AsceMass/Fragments/AsceOrderFragments.h"
#include "AsceMass/Fragments/AsceMovementFragments.h"
#include "AsceMass/Movement/AsceMoveOrderExecutor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceOrderChannelComponent)

UAsceOrderChannelComponent::UAsceOrderChannelComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

// ──────────────────────────────────────────
// RequestMoveOrder（客户端本地入口，非RPC）
// ──────────────────────────────────────────

void UAsceOrderChannelComponent::RequestMoveOrder(const TArray<FMassEntityHandle>& LocalSelectedHandles, FVector2D GoalWorldLocation, float AcceptanceRadiusSquared)
{
	if (LocalSelectedHandles.Num() == 0)
	{
		return;
	}

	const UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!IsValid(World) || !IsValid(Owner))
	{
		return;
	}

	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (!MassEntitySubsystem)
	{
		return;
	}
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();

	const int32 OrderSequence = ++NextOrderSequence;

	// 读本地NetID拼RPC参数——本地selection本来就是EntityHandle，NetID只是给服务器反查用。
	TArray<FMassNetworkID> NetIDs;
	NetIDs.Reserve(LocalSelectedHandles.Num());
	for (const FMassEntityHandle& Handle : LocalSelectedHandles)
	{
		if (!EntityManager.IsEntityValid(Handle))
		{
			continue;
		}
		const FMassNetworkIDFragment* NetIDFragment = EntityManager.GetFragmentDataPtr<FMassNetworkIDFragment>(Handle);
		if (NetIDFragment)
		{
			NetIDs.Add(NetIDFragment->NetID);
		}
	}
	if (NetIDs.Num() == 0)
	{
		return;
	}

	ServerRequestMoveOrder(NetIDs, GoalWorldLocation, AcceptanceRadiusSquared, OrderSequence);

	// 本地预测：只有真正的纯客户端才需要——Standalone/ListenServer下Owner本身就是Authority，
	// 上面这行RPC调用会在本地同步直接跑到_Implementation，这里如果也执行一遍会让PathPlan
	// 被多Acquire一次，造成引用计数泄漏，所以要用HasAuthority()挡住。
	if (Owner->HasAuthority())
	{
		return;
	}

	UAscePathFindingSubsystem* PathFindingSubsystem = UAscePathFindingSubsystem::Get(this);
	UPathFinder* PathFinder = PathFindingSubsystem ? PathFindingSubsystem->GetPathFinder() : nullptr;
	if (!PathFinder)
	{
		return;
	}

	TArray<FMassEntityHandle> ValidHandles;
	TArray<FVector2D> EntityWorldLocations2D;
	ValidHandles.Reserve(LocalSelectedHandles.Num());
	EntityWorldLocations2D.Reserve(LocalSelectedHandles.Num());
	for (const FMassEntityHandle& Handle : LocalSelectedHandles)
	{
		if (!EntityManager.IsEntityValid(Handle))
		{
			continue;
		}
		const FAsceMoveOrderFragment* MoveOrder = EntityManager.GetFragmentDataPtr<FAsceMoveOrderFragment>(Handle);
		const FTransformFragment* TransformFragment = EntityManager.GetFragmentDataPtr<FTransformFragment>(Handle);
		if (!MoveOrder || !TransformFragment)
		{
			continue;
		}
		const FVector CurrentLocation = TransformFragment->GetTransform().GetLocation();
		ValidHandles.Add(Handle);
		EntityWorldLocations2D.Add(FVector2D(CurrentLocation.X, CurrentLocation.Y));
	}

	if (ValidHandles.Num() == 0)
	{
		return;
	}

	FAsceMoveOrderExecutor::ApplyMergedMoveOrder(EntityManager, *PathFinder, ValidHandles, EntityWorldLocations2D, GoalWorldLocation, OrderSequence);

	// 记下这次本地预测实际生效的entity，供后续拒绝回执反查；超过容量丢最旧的一条
	// （理论上不该堆积到这个量级，正常订单几帧内就该收到服务器确认/拒绝）。
	if (PendingLocalOrders.Num() >= PendingLocalOrdersCapacity)
	{
		int32 OldestSequence = TNumericLimits<int32>::Max();
		for (const TPair<int32, TArray<FMassEntityHandle>>& Pair : PendingLocalOrders)
		{
			OldestSequence = FMath::Min(OldestSequence, Pair.Key);
		}
		PendingLocalOrders.Remove(OldestSequence);
	}
	PendingLocalOrders.Add(OrderSequence, MoveTemp(ValidHandles));
}

// ──────────────────────────────────────────
// ServerRequestMoveOrder
// ──────────────────────────────────────────

bool UAsceOrderChannelComponent::ServerRequestMoveOrder_Validate(const TArray<FMassNetworkID>& SelectedNetIDs, FVector2D GoalWorldLocation, float AcceptanceRadiusSquared, int32 OrderSequence)
{
	// 只做"值本身是否合法/请求规模是否离谱"这类无关业务的硬性校验，
	// 校验不通过会触发引擎自带的作弊检测断线流程，所以像"NetID查不到entity"这种正常业务失败
	// 不能放在这里，要放到_Implementation里用ClientRejectEntityOrder温和地回传。
	if (SelectedNetIDs.Num() == 0 || SelectedNetIDs.Num() > MaxOrderSelectionCount)
	{
		return false;
	}
	if (!FMath::IsFinite(GoalWorldLocation.X) || !FMath::IsFinite(GoalWorldLocation.Y))
	{
		return false;
	}
	return true;
}

void UAsceOrderChannelComponent::ServerRequestMoveOrder_Implementation(const TArray<FMassNetworkID>& SelectedNetIDs, FVector2D GoalWorldLocation, float AcceptanceRadiusSquared, int32 OrderSequence)
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	UAsceNetIDRegistrySubsystem* Registry = UAsceNetIDRegistrySubsystem::Get(this);
	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	UAscePathFindingSubsystem* PathFindingSubsystem = UAscePathFindingSubsystem::Get(this);
	UPathFinder* PathFinder = PathFindingSubsystem ? PathFindingSubsystem->GetPathFinder() : nullptr;
	if (!Registry || !MassEntitySubsystem || !PathFinder)
	{
		// 依赖的Subsystem缺失，这一批entity一个都没处理——整批拒绝。
		ClientRejectWholeOrder(OrderSequence, EAsceOrderRejectReason::InvalidTarget);
		return;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();

	// 第一轮：逐个校验NetID→EntityHandle→Order Fragment是否都齐备，收集有效entity的当前位置，
	// 校验失败的单独回传拒绝原因（针对该entity），不影响批里其它entity继续下单。
	TArray<FMassEntityHandle> ValidHandles;
	TArray<FVector2D> EntityWorldLocations2D;
	ValidHandles.Reserve(SelectedNetIDs.Num());
	EntityWorldLocations2D.Reserve(SelectedNetIDs.Num());

	for (const FMassNetworkID& NetID : SelectedNetIDs)
	{
		const FMassEntityHandle Handle = Registry->FindEntity(NetID);
		if (!EntityManager.IsEntityValid(Handle))
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::EntityNotFound);
			continue;
		}

		const FAsceMoveOrderFragment* MoveOrder = EntityManager.GetFragmentDataPtr<FAsceMoveOrderFragment>(Handle);
		const FTransformFragment* TransformFragment = EntityManager.GetFragmentDataPtr<FTransformFragment>(Handle);
		if (!MoveOrder || !TransformFragment)
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::MissingOrderFragment);
			continue;
		}

		const FVector CurrentLocation = TransformFragment->GetTransform().GetLocation();
		ValidHandles.Add(Handle);
		EntityWorldLocations2D.Add(FVector2D(CurrentLocation.X, CurrentLocation.Y));
	}

	if (ValidHandles.Num() == 0)
	{
		// 批里每个entity都已经各自收到过ClientRejectEntityOrder了，这里不需要再补一次整批拒绝。
		return;
	}

	// 第二轮：Server和Client本地预测共用同一份写入逻辑，真正利用"Mergeable"能力——
	// 这一批entity共享同一个目标，只发起一次A*请求。
	FAsceMoveOrderExecutor::ApplyMergedMoveOrder(EntityManager, *PathFinder, ValidHandles, EntityWorldLocations2D, GoalWorldLocation, AcceptanceRadiusSquared, OrderSequence);
}

// ──────────────────────────────────────────
// ServerRequestAttackOrder
// ──────────────────────────────────────────

bool UAsceOrderChannelComponent::ServerRequestAttackOrder_Validate(const TArray<FMassNetworkID>& SelectedNetIDs, FMassNetworkID TargetNetID, int32 OrderSequence)
{
	if (SelectedNetIDs.Num() == 0 || SelectedNetIDs.Num() > MaxOrderSelectionCount)
	{
		return false;
	}
	return true;
}

void UAsceOrderChannelComponent::ServerRequestAttackOrder_Implementation(const TArray<FMassNetworkID>& SelectedNetIDs, FMassNetworkID TargetNetID, int32 OrderSequence)
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	UAsceNetIDRegistrySubsystem* Registry = UAsceNetIDRegistrySubsystem::Get(this);
	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (!Registry || !MassEntitySubsystem)
	{
		ClientRejectWholeOrder(OrderSequence, EAsceOrderRejectReason::InvalidTarget);
		return;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();

	// 攻击目标本身也要能查到，查不到直接整批拒绝（跟移动指令不同，移动指令目标是坐标，
	// 不存在"目标entity失效"这一档；攻击指令一旦目标失效，整批指令没有意义）。
	const FMassEntityHandle TargetHandle = Registry->FindEntity(TargetNetID);
	if (!EntityManager.IsEntityValid(TargetHandle))
	{
		ClientRejectWholeOrder(OrderSequence, EAsceOrderRejectReason::InvalidTarget);
		return;
	}

	for (const FMassNetworkID& NetID : SelectedNetIDs)
	{
		const FMassEntityHandle Handle = Registry->FindEntity(NetID);
		if (!EntityManager.IsEntityValid(Handle))
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::EntityNotFound);
			continue;
		}

		FAsceAttackOrderFragment* AttackOrder = EntityManager.GetFragmentDataPtr<FAsceAttackOrderFragment>(Handle);
		if (!AttackOrder)
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::MissingOrderFragment);
			continue;
		}

		AttackOrder->TargetNetID = TargetNetID;
		AttackOrder->bHasOrder = true;
		AttackOrder->OrderSequence = OrderSequence;
	}
}

// ──────────────────────────────────────────
// ClientRejectWholeOrder / ClientRejectEntityOrder
// ──────────────────────────────────────────

void UAsceOrderChannelComponent::ClientRejectWholeOrder_Implementation(int32 OrderSequence, EAsceOrderRejectReason Reason)
{
	TArray<FMassEntityHandle> Handles;
	if (!PendingLocalOrders.RemoveAndCopyValue(OrderSequence, Handles))
	{
		// 不是这次本地预测过的订单（比如这个客户端本身就是Authority，从没往PendingLocalOrders里记过），
		// 没有需要回滚的本地状态。
		return;
	}

	const UWorld* World = GetWorld();
	UMassEntitySubsystem* MassEntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	UAscePathFindingSubsystem* PathFindingSubsystem = UAscePathFindingSubsystem::Get(this);
	UPathFinder* PathFinder = PathFindingSubsystem ? PathFindingSubsystem->GetPathFinder() : nullptr;
	if (!MassEntitySubsystem || !PathFinder)
	{
		return;
	}
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();

	for (const FMassEntityHandle& Handle : Handles)
	{
		if (!EntityManager.IsEntityValid(Handle))
		{
			continue;
		}
		FAsceMoveOrderFragment* MoveOrder = EntityManager.GetFragmentDataPtr<FAsceMoveOrderFragment>(Handle);
		// OrderSequence要对得上才回滚——避免这个entity后来又被下了一次新命令，
		// 结果被一个"迟到"的旧订单拒绝回执误伤。
		if (!MoveOrder || MoveOrder->OrderSequence != OrderSequence)
		{
			continue;
		}
		// 只有"订单还在执行中"才需要/才能 Release（已到达的订单在到达时已经 Release 过，
		// Handle 只保留 RawID 供组重置用；再 Release 一次会破坏其它组员的引用计数）。
		if (MoveOrder->bHasOrder && MoveOrder->PathPlanHandle.IsValid())
		{
			PathFinder->ReleasePathPlan(MoveOrder->PathPlanHandle);
		}
		// 订单作废：清零 Handle（不再是该方案组员），回到"无寻路"状态
		MoveOrder->PathPlanHandle = FMergedPathPlanHandle();
		MoveOrder->bHasOrder = false;
		// 订单被拒绝/取消 = 回到"无寻路"状态（bArrived 默认语义，见 FAsceMovementSimStateFragment）
		if (FAsceMovementSimStateFragment* SimState = EntityManager.GetFragmentDataPtr<FAsceMovementSimStateFragment>(Handle))
		{
			SimState->bArrived = true;
		}
	}
}

void UAsceOrderChannelComponent::ClientRejectEntityOrder_Implementation(int32 OrderSequence, FMassNetworkID UnitNetID, EAsceOrderRejectReason Reason)
{
	TArray<FMassEntityHandle>* HandlesPtr = PendingLocalOrders.Find(OrderSequence);
	if (!HandlesPtr)
	{
		return;
	}

	const UWorld* World = GetWorld();
	UMassEntitySubsystem* MassEntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	UAscePathFindingSubsystem* PathFindingSubsystem = UAscePathFindingSubsystem::Get(this);
	UPathFinder* PathFinder = PathFindingSubsystem ? PathFindingSubsystem->GetPathFinder() : nullptr;
	if (!MassEntitySubsystem || !PathFinder)
	{
		return;
	}
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();

	// 在这批本地已知的entity里找NetID匹配的那一个——不需要全局NetID反查表，
	// PendingLocalOrders本来存的就是本地EntityHandle，直接读它们各自的FMassNetworkIDFragment比对即可。
	for (int32 Index = 0; Index < HandlesPtr->Num(); ++Index)
	{
		const FMassEntityHandle Handle = (*HandlesPtr)[Index];
		if (!EntityManager.IsEntityValid(Handle))
		{
			continue;
		}
		const FMassNetworkIDFragment* NetIDFragment = EntityManager.GetFragmentDataPtr<FMassNetworkIDFragment>(Handle);
		if (!NetIDFragment || !(NetIDFragment->NetID == UnitNetID))
		{
			continue;
		}

		FAsceMoveOrderFragment* MoveOrder = EntityManager.GetFragmentDataPtr<FAsceMoveOrderFragment>(Handle);
		if (MoveOrder && MoveOrder->OrderSequence == OrderSequence)
		{
			// 只有"订单还在执行中"才需要/才能 Release（已到达的订单在到达时已经 Release 过，
			// Handle 只保留 RawID 供组重置用；再 Release 一次会破坏其它组员的引用计数）。
			if (MoveOrder->bHasOrder && MoveOrder->PathPlanHandle.IsValid())
			{
				PathFinder->ReleasePathPlan(MoveOrder->PathPlanHandle);
			}
			MoveOrder->PathPlanHandle = FMergedPathPlanHandle();
			MoveOrder->bHasOrder = false;
			// 订单被拒绝/取消 = 回到"无寻路"状态（bArrived 默认语义）
			if (FAsceMovementSimStateFragment* SimState = EntityManager.GetFragmentDataPtr<FAsceMovementSimStateFragment>(Handle))
			{
				SimState->bArrived = true;
			}
		}

		HandlesPtr->RemoveAtSwap(Index);
		break;
	}

	if (HandlesPtr->Num() == 0)
	{
		PendingLocalOrders.Remove(OrderSequence);
	}
}
