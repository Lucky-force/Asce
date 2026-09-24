// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceOrderChannelComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Math/NumericLimits.h"
#include "MassEntitySubsystem.h"
#include "MassCommonFragments.h"
#include "GenericTeamAgentInterface.h"
#include "Teams/AsceTeamAgentInterface.h"   // GenericTeamIdToInteger
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "MassReplicationFragments.h"
#include "AsceMass/Networking/AsceNetIDRegistrySubsystem.h"
#include "AsceMass/FlowField/AscePathFindingSubsystem.h"
#include "AsceMass/FlowField/AscePathFinder.h"
#include "AsceMass/Fragments/AsceOrderFragments.h"
#include "AsceMass/Combat/AsceCombatFragments.h"   // FAsceActionFragment/FAsceSkillSlotsFragment（停止与技能槽位）
#include "AsceMass/Combat/AsceActionDefLibrary.h"  // 技能（动作定义）查表校验
#include "GameModes/AsceGameState.h"               // 服务器世界时间（CD 判定）
#include "AsceMass/Fragments/AsceMovementFragments.h"
#include "AsceMass/Movement/AsceMoveOrderExecutor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceOrderChannelComponent)

UAsceOrderChannelComponent::UAsceOrderChannelComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

namespace
{
	/** 单个实体的"停止"效果（客户端入口与服务器实现共用）：
	 *  - **清移动命令**：项目惯用法 = `FAsceMoveOrderFragment::bHasOrder = false`；
	 *  - **取消动作**：施法（`Cast`/`Channel`）与攻击都回 `Idle`。
	 *    `Cast` 未到"真正释放"⇒ 完全不付费；`Channel` 已生效效果保留、蓝/CD 不返还；
	 *    攻击清零后会由扫描按"自动攻击开关"重新起手（**停止不影响自动攻击开关**）。 */
	void ApplyStopToEntity(FMassEntityManager& EntityManager, const FMassEntityHandle Handle)
	{
		if (FAsceMoveOrderFragment* MoveOrder = EntityManager.GetFragmentDataPtr<FAsceMoveOrderFragment>(Handle))
		{
			MoveOrder->bHasOrder = false;
		}
		if (FAsceActionFragment* Action = EntityManager.GetFragmentDataPtr<FAsceActionFragment>(Handle))
		{
			if (Action->ActionState != EAsceActionState::Idle && Action->ActionState != EAsceActionState::Dying)
			{
				Action->ActionState = EAsceActionState::Idle;
				Action->ActionTarget.Reset();
				++Action->Counter;
			}
		}
	}
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

int32 UAsceOrderChannelComponent::GetRequesterTeamId()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	// 团队身份来源：Controller（没有则退回 Pawn）的 GenericTeamId；NoTeam/取不到 → 0 中立
	IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(Controller);
	if (!TeamAgent && Pawn)
	{
		TeamAgent = Cast<IGenericTeamAgentInterface>(Pawn);
	}
	return TeamAgent ? AsceNormalizeTeamId(GenericTeamIdToInteger(TeamAgent->GetGenericTeamId())) : AsceNeutralTeamId;
}

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

	const int32 RequesterTeamId = GetRequesterTeamId();

	for (const FMassNetworkID& NetID : SelectedNetIDs)
	{
		const FMassEntityHandle Handle = Registry->FindEntity(NetID);
		if (!EntityManager.IsEntityValid(Handle))
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::EntityNotFound);
			continue;
		}

		// ownership：只能指挥自己阵营的实体（**TeamId** 比较，客户端已先过滤，这里是防作弊兜底）
		const FAsceTeamFragment* TeamFragment = EntityManager.GetFragmentDataPtr<FAsceTeamFragment>(Handle);
		if (!TeamFragment || AsceTeamId(TeamFragment->TeamKey) != RequesterTeamId)
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::NotOwned);
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

	const int32 RequesterTeamId = GetRequesterTeamId();

	for (const FMassNetworkID& NetID : SelectedNetIDs)
	{
		const FMassEntityHandle Handle = Registry->FindEntity(NetID);
		if (!EntityManager.IsEntityValid(Handle))
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::EntityNotFound);
			continue;
		}

		// ownership：只能指挥自己阵营的实体（点选可以选中敌方来查看，但不能指挥它去打人）
		const FAsceTeamFragment* TeamFragment = EntityManager.GetFragmentDataPtr<FAsceTeamFragment>(Handle);
		if (!TeamFragment || AsceTeamId(TeamFragment->TeamKey) != RequesterTeamId)
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::NotOwned);
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
// RequestSkillOrder / ServerRequestSkillOrder（释放技能）
// ──────────────────────────────────────────

void UAsceOrderChannelComponent::RequestSkillOrder(const TArray<FMassEntityHandle>& LocalSelectedHandles, int32 SlotId, FVector2D TargetPos, FMassNetworkID TargetNetID)
{
	if (LocalSelectedHandles.Num() == 0)
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (!IsValid(World))
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

	TArray<FMassNetworkID> NetIDs;
	NetIDs.Reserve(LocalSelectedHandles.Num());
	for (const FMassEntityHandle& Handle : LocalSelectedHandles)
	{
		if (!EntityManager.IsEntityValid(Handle))
		{
			continue;
		}
		if (const FMassNetworkIDFragment* NetIDFragment = EntityManager.GetFragmentDataPtr<FMassNetworkIDFragment>(Handle))
		{
			NetIDs.Add(NetIDFragment->NetID);
		}
	}
	if (NetIDs.Num() == 0)
	{
		return;
	}

	// v1 不做预测/回执：施法由服务器权威处理，表现走状态复制（见 `Combat/技能系统设计.md` §3.1）
	ServerRequestSkillOrder(NetIDs, SlotId, TargetPos, TargetNetID, OrderSequence);
}

bool UAsceOrderChannelComponent::ServerRequestSkillOrder_Validate(const TArray<FMassNetworkID>& SelectedNetIDs, int32 SlotId, FVector2D TargetPos, FMassNetworkID TargetNetID, int32 OrderSequence)
{
	return SelectedNetIDs.Num() > 0 && SelectedNetIDs.Num() <= MaxOrderSelectionCount;
}

void UAsceOrderChannelComponent::ServerRequestSkillOrder_Implementation(const TArray<FMassNetworkID>& SelectedNetIDs, int32 SlotId, FVector2D TargetPos, FMassNetworkID TargetNetID, int32 OrderSequence)
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}
	if (SlotId < 0 || SlotId >= FAsceSkillSlotsFragment::NumSkillSlots)
	{
		ClientRejectWholeOrder(OrderSequence, EAsceOrderRejectReason::InvalidTarget);
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
	const AAsceGameState* GameState = World->GetGameState<AAsceGameState>();
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	const int32 RequesterTeamId = GetRequesterTeamId();

	// 指向性技能的目标（客户端只能点到"已复制到自己这里"的单位 ⇒ 天然不会隔雾点人；服务器照样兜底校验）
	FMassEntityHandle TargetHandle;
	if (TargetNetID.IsValid())
	{
		TargetHandle = Registry->FindEntity(TargetNetID);
	}

	for (const FMassNetworkID& NetID : SelectedNetIDs)
	{
		const FMassEntityHandle Handle = Registry->FindEntity(NetID);
		if (!EntityManager.IsEntityValid(Handle))
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::EntityNotFound);
			continue;
		}
		const FAsceTeamFragment* TeamFragment = EntityManager.GetFragmentDataPtr<FAsceTeamFragment>(Handle);
		if (!TeamFragment || AsceTeamId(TeamFragment->TeamKey) != RequesterTeamId)
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::NotOwned);
			continue;
		}

		// ── ① **无条件停止移动**（即使技能放不出来也要停：否则框选里"有的放有的还在走"很诡异，见 §3.1）──
		if (FAsceMoveOrderFragment* MoveOrder = EntityManager.GetFragmentDataPtr<FAsceMoveOrderFragment>(Handle))
		{
			MoveOrder->bHasOrder = false;
		}

		// ── ② 校验：槽位非空 / 技能存在 / CD / 蓝 / 目标 / 射程（任一不满足 ⇒ 只停不放）──
		FAsceSkillSlotsFragment* Slots = EntityManager.GetFragmentDataPtr<FAsceSkillSlotsFragment>(Handle);
		if (!Slots || Slots->Slots[SlotId].ActionId == 0)
		{
			continue;
		}
		FAsceActionDef ActionDef;
		if (!FAsceActionDefLibrary::GetActionDef(Slots->Slots[SlotId].ActionId, ActionDef))
		{
			continue;   // 技能表查不到（配置错误）
		}
		if (Slots->Slots[SlotId].NextReadyServerTime > Now)
		{
			continue;   // CD 中
		}
		if (const FAsceManaFragment* Mana = EntityManager.GetFragmentDataPtr<FAsceManaFragment>(Handle))
		{
			if (Mana->CurrentMana < ActionDef.ManaCost)
			{
				continue;   // 蓝不够
			}
		}
		FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Handle);
		if (!Transform)
		{
			continue;
		}
		const FVector Location = Transform->GetTransform().GetLocation();

		FVector AimLocation(TargetPos.X, TargetPos.Y, Location.Z);
		if (ActionDef.TargetKind == EAsceTargetKind::Unit)
		{
			// 指向性：没点到有效单位 ⇒ 什么都不发生（只停）
			if (!TargetHandle.IsValid() || !EntityManager.IsEntityValid(TargetHandle) || TargetHandle == Handle)
			{
				continue;
			}
			if (const FTransformFragment* TargetTransform = EntityManager.GetFragmentDataPtr<FTransformFragment>(TargetHandle))
			{
				AimLocation = TargetTransform->GetTransform().GetLocation();
			}
		}
		else if (ActionDef.TargetKind == EAsceTargetKind::None)
		{
			AimLocation = Location;
		}

		// 射程（v1：圆心距离 + 双方半径；高度口径后补）——不够就**不释放、也不走过去**
		float TargetRadius = 0.f;
		if (TargetHandle.IsValid())
		{
			if (const FAgentRadiusFragment* TargetRadiusFragment = EntityManager.GetFragmentDataPtr<FAgentRadiusFragment>(TargetHandle))
			{
				TargetRadius = TargetRadiusFragment->Radius;
			}
		}
		float SelfRadius = 0.f;
		if (const FAgentRadiusFragment* SelfRadiusFragment = EntityManager.GetFragmentDataPtr<FAgentRadiusFragment>(Handle))
		{
			SelfRadius = SelfRadiusFragment->Radius;
		}
		if (ActionDef.Range > 0.f
			&& FVector::Dist2D(Location, AimLocation) > ActionDef.Range + SelfRadius + TargetRadius)
		{
			continue;
		}

		// ── ③ 进动作（技能优先级最高：可顶掉攻击与施法；Cast 未到释放点 ⇒ 不结算，Channel 已生效保留）──
		// ⚠️ 施法推进（前摇结束 = **真正释放**：扣蓝/落 CD/effect 入队；Channel 周期结算）在**下一步的 processor** 里；
		//    在它落地前，单位进了 Cast/Channel **不会自动结束** —— 接线前先别实际放技能。
		FAsceActionFragment* Action = EntityManager.GetFragmentDataPtr<FAsceActionFragment>(Handle);
		if (!Action)
		{
			continue;
		}
		const float FacingYaw = FMath::RadiansToDegrees(FMath::Atan2(AimLocation.Y - Location.Y, AimLocation.X - Location.X));
		const bool bChannel = (ActionDef.CastKind == EAsceCastKind::Channel);
		Action->ActionState = bChannel ? EAsceActionState::Channel : EAsceActionState::Cast;
		Action->ActionDefId = Slots->Slots[SlotId].ActionId;
		Action->ActionTarget = TargetHandle;
		Action->ActionTargetPos = FVector2D(AimLocation.X, AimLocation.Y);
		Action->FacingYaw = FacingYaw;
		Action->ActionEndTime = Now + (bChannel ? ActionDef.ChannelDuration : FMath::Max(ActionDef.CastTime, 0.f));
		Action->NextTickTime = bChannel ? (Now + FMath::Max(ActionDef.ChannelTickInterval, 0.01f)) : 0.0;
		++Action->Counter;

		// **`Channel` 无前摇 ⇒ 进入那一刻就是"真正释放"**：立刻扣蓝 + 落 CD（槽位）。
		// `Cast` 的 cost/CD 在"施法推进"里、前摇结束那一帧才处理（见 `Combat/技能系统设计.md` §5）
		if (bChannel)
		{
			if (FAsceManaFragment* Mana = EntityManager.GetFragmentDataPtr<FAsceManaFragment>(Handle))
			{
				Mana->CurrentMana = FMath::Max(0.f, Mana->CurrentMana - ActionDef.ManaCost);
			}
			Slots->Slots[SlotId].NextReadyServerTime = Now + ActionDef.Cooldown;
		}

		// 动作开始时对齐朝向（服务器权威朝向；客户端表现靠动作态通道的 FacingQ）
		Transform->GetMutableTransform().SetRotation(FRotator(0.f, FacingYaw, 0.f).Quaternion());

		UE_LOG(LogTemp, Verbose, TEXT("[Skill] %lld 开始施法：技能 %d（槽 %d，CastKind %d）"),
			static_cast<int64>(Handle.AsNumber()), Action->ActionDefId, SlotId, static_cast<int32>(ActionDef.CastKind));
	}
}

// ──────────────────────────────────────────
// RequestSetSkillSlot / ServerRequestSetSkillSlot（配置技能槽位）
// ──────────────────────────────────────────

void UAsceOrderChannelComponent::RequestSetSkillSlot(const TArray<FMassEntityHandle>& LocalSelectedHandles, int32 SlotIndex, int32 ActionId)
{
	if (LocalSelectedHandles.Num() == 0)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!IsValid(World))
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

	TArray<FMassNetworkID> NetIDs;
	NetIDs.Reserve(LocalSelectedHandles.Num());
	for (const FMassEntityHandle& Handle : LocalSelectedHandles)
	{
		if (!EntityManager.IsEntityValid(Handle))
		{
			continue;
		}
		if (const FMassNetworkIDFragment* NetIDFragment = EntityManager.GetFragmentDataPtr<FMassNetworkIDFragment>(Handle))
		{
			NetIDs.Add(NetIDFragment->NetID);
		}
	}
	if (NetIDs.Num() == 0)
	{
		return;
	}

	ServerRequestSetSkillSlot(NetIDs, SlotIndex, ActionId, OrderSequence);
}

bool UAsceOrderChannelComponent::ServerRequestSetSkillSlot_Validate(const TArray<FMassNetworkID>& SelectedNetIDs, int32 SlotIndex, int32 ActionId, int32 OrderSequence)
{
	return SelectedNetIDs.Num() > 0 && SelectedNetIDs.Num() <= MaxOrderSelectionCount;
}

void UAsceOrderChannelComponent::ServerRequestSetSkillSlot_Implementation(const TArray<FMassNetworkID>& SelectedNetIDs, int32 SlotIndex, int32 ActionId, int32 OrderSequence)
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}
	// 槽位下标与技能合法性先做一次（与单位无关的检查；单位的"有没有这技能"逐单位查）
	if (SlotIndex < 0 || SlotIndex >= FAsceSkillSlotsFragment::NumSkillSlots)
	{
		ClientRejectWholeOrder(OrderSequence, EAsceOrderRejectReason::InvalidTarget);
		return;
	}
	if (ActionId != 0)
	{
		FAsceActionDef ActionDef;
		if (!FAsceActionDefLibrary::GetActionDef(ActionId, ActionDef)
			|| ActionDef.SkillType != EAsceSkillType::Active)
		{
			ClientRejectWholeOrder(OrderSequence, EAsceOrderRejectReason::InvalidTarget);
			return;
		}
	}

	UAsceNetIDRegistrySubsystem* Registry = UAsceNetIDRegistrySubsystem::Get(this);
	UMassEntitySubsystem* MassEntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (!Registry || !MassEntitySubsystem)
	{
		ClientRejectWholeOrder(OrderSequence, EAsceOrderRejectReason::InvalidTarget);
		return;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	const int32 RequesterTeamId = GetRequesterTeamId();

	for (const FMassNetworkID& NetID : SelectedNetIDs)
	{
		const FMassEntityHandle Handle = Registry->FindEntity(NetID);
		if (!EntityManager.IsEntityValid(Handle))
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::EntityNotFound);
			continue;
		}
		const FAsceTeamFragment* TeamFragment = EntityManager.GetFragmentDataPtr<FAsceTeamFragment>(Handle);
		if (!TeamFragment || AsceTeamId(TeamFragment->TeamKey) != RequesterTeamId)
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::NotOwned);
			continue;
		}
		FAsceSkillSlotsFragment* Slots = EntityManager.GetFragmentDataPtr<FAsceSkillSlotsFragment>(Handle);
		if (!Slots)
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::MissingOrderFragment);
			continue;
		}

		// **槽位 = 技能的容器**（技能运行时状态随槽位，见 `Combat/技能系统设计.md` §2.2）：
		//  - 该技能已在别的槽位 ⇒ **两槽交换**（技能连同 CD 一起换位，不重置）；
		//  - 新技能 ⇒ 写入并 CD 归零；
		//  - `ActionId == 0` ⇒ 只清空该槽位（不牵动别的槽位）。
		int32 SourceIndex = INDEX_NONE;
		if (ActionId != 0)
		{
			for (int32 Index = 0; Index < FAsceSkillSlotsFragment::NumSkillSlots; ++Index)
			{
				if (Index != SlotIndex && Slots->Slots[Index].ActionId == ActionId)
				{
					SourceIndex = Index;
					break;
				}
			}
		}

		if (SourceIndex != INDEX_NONE)
		{
			Swap(Slots->Slots[SlotIndex], Slots->Slots[SourceIndex]);
		}
		else
		{
			Slots->Slots[SlotIndex].ActionId = ActionId;
			Slots->Slots[SlotIndex].NextReadyServerTime = 0.0;
		}

		UE_LOG(LogTemp, Verbose, TEXT("[Skill] 槽位配置：%lld 槽 %d = %d（交换源 %d）"),
			static_cast<int64>(Handle.AsNumber()), SlotIndex, ActionId, SourceIndex);
	}
}

// ──────────────────────────────────────────
// RequestStopOrder / ServerRequestStopOrder（`S` 停止指令）
// ──────────────────────────────────────────

void UAsceOrderChannelComponent::RequestStopOrder(const TArray<FMassEntityHandle>& LocalSelectedHandles)
{
	if (LocalSelectedHandles.Num() == 0)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!IsValid(World))
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

	// 本地 selection 本来就是 EntityHandle；NetID 只给服务器反查用
	TArray<FMassNetworkID> NetIDs;
	NetIDs.Reserve(LocalSelectedHandles.Num());
	for (const FMassEntityHandle& Handle : LocalSelectedHandles)
	{
		if (!EntityManager.IsEntityValid(Handle))
		{
			continue;
		}
		if (const FMassNetworkIDFragment* NetIDFragment = EntityManager.GetFragmentDataPtr<FMassNetworkIDFragment>(Handle))
		{
			NetIDs.Add(NetIDFragment->NetID);
		}
	}
	if (NetIDs.Num() == 0)
	{
		return;
	}

	// v1 不做本地预测：停止由服务器权威处理（与技能一致，见 `Combat/技能系统设计.md` §3.2）
	ServerRequestStopOrder(NetIDs, OrderSequence);
}

bool UAsceOrderChannelComponent::ServerRequestStopOrder_Validate(const TArray<FMassNetworkID>& SelectedNetIDs, int32 OrderSequence)
{
	return SelectedNetIDs.Num() > 0 && SelectedNetIDs.Num() <= MaxOrderSelectionCount;
}

void UAsceOrderChannelComponent::ServerRequestStopOrder_Implementation(const TArray<FMassNetworkID>& SelectedNetIDs, int32 OrderSequence)
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
	const int32 RequesterTeamId = GetRequesterTeamId();

	for (const FMassNetworkID& NetID : SelectedNetIDs)
	{
		const FMassEntityHandle Handle = Registry->FindEntity(NetID);
		if (!EntityManager.IsEntityValid(Handle))
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::EntityNotFound);
			continue;
		}
		// ownership：只能指挥自己阵营的实体（防作弊兜底）
		const FAsceTeamFragment* TeamFragment = EntityManager.GetFragmentDataPtr<FAsceTeamFragment>(Handle);
		if (!TeamFragment || AsceTeamId(TeamFragment->TeamKey) != RequesterTeamId)
		{
			ClientRejectEntityOrder(OrderSequence, NetID, EAsceOrderRejectReason::NotOwned);
			continue;
		}

		ApplyStopToEntity(EntityManager, Handle);
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
