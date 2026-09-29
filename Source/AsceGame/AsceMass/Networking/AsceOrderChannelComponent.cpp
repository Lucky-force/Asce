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
#include "AsceMass/Vision/AsceFogSubsystem.h"   // 施法可见性兜底（防篡改客户端隔雾点人）
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
	 *  - **取消动作**：施法（`Casting`/`Channel`）与攻击都回 `Idle`。
	 *    `Casting` 未到"真正释放"⇒ 完全不付费；`Channel` 已生效效果保留、蓝/CD 不返还；
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

	/** 记下这次下单实际用到的本地 entity（供拒绝回执反查 + 撤销预测）；超过容量丢最旧一条。 */
	void RecordPendingLocalOrder(TMap<int32, TArray<FMassEntityHandle>>& PendingLocalOrders, const int32 Capacity
		, const int32 OrderSequence, TArray<FMassEntityHandle>&& Handles)
	{
		if (PendingLocalOrders.Num() >= Capacity)
		{
			int32 OldestSequence = TNumericLimits<int32>::Max();
			for (const TPair<int32, TArray<FMassEntityHandle>>& Pair : PendingLocalOrders)
			{
				OldestSequence = FMath::Min(OldestSequence, Pair.Key);
			}
			PendingLocalOrders.Remove(OldestSequence);
		}
		PendingLocalOrders.Add(OrderSequence, MoveTemp(Handles));
	}

	/** 客户端技能预测（**只预测表现，不预测结算**；`Combat/技能系统设计.md` §3.1）：
	 *  拿本地已复制数据跑一遍**和服务器同序**的校验，通过就写一次 `FAsceCombatVisualFragment`
	 *  （`Casting` + 技能 id + `Counter++` + 朝向）⇒ 动画/转身立刻跑起来。不通过就不预测（指令照发）。
	 *  权威动作态边沿到达会**整段覆写**它 ⇒ 天然就是"隐式确认/取消"。
	 *  客户端缺的东西：迷雾（客户端只有复制过来的实体，"可见"天然满足）、`FAsceCombatFragment`（服务器专有；
	 *  技能射程从技能表取）。CD 是回绕百分秒值（见 `AsceClientIsSkillCooldownReady`）。 */
	void PredictSkillOrderLocally(FMassEntityManager& EntityManager, const TArray<FMassEntityHandle>& Handles, const int32 SlotId
		, const FVector2D TargetPos, const FMassEntityHandle TargetHandle, const int32 MyTeamId, const double ServerWorldTimeSeconds)
	{
		const bool bTargetIsUnit = TargetHandle.IsValid() && EntityManager.IsEntityValid(TargetHandle);

		for (const FMassEntityHandle Handle : Handles)
		{
			if (!EntityManager.IsEntityValid(Handle))
			{
				continue;
			}

			// ── ① 槽位 / 技能表 / CastConfig（与服务器同序）──
			const FAsceSkillSlotsFragment* Slots = EntityManager.GetFragmentDataPtr<FAsceSkillSlotsFragment>(Handle);
			if (!Slots || Slots->Slots[SlotId].ActionId == 0)
			{
				continue;
			}
			FAsceActionDef ActionDef;
			if (!FAsceActionDefLibrary::GetActionDef(Slots->Slots[SlotId].ActionId, ActionDef) || !ActionDef.CastConfig.IsValid())
			{
				continue;
			}

			// ── ② CD（客户端是回绕百分秒）/ 蓝 ──
			if (!AsceClientIsSkillCooldownReady(Slots->Slots[SlotId].NextReadyServerTime, ServerWorldTimeSeconds))
			{
				continue;
			}
			if (const FAsceManaFragment* Mana = EntityManager.GetFragmentDataPtr<FAsceManaFragment>(Handle))
			{
				if (Mana->CurrentMana < ActionDef.ManaCost)
				{
					continue;
				}
			}

			// ── ③ 位置 / 目标 / 射程（几何口径与服务器一致）──
			const FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Handle);
			if (!Transform)
			{
				continue;
			}
			const FVector Location = Transform->GetTransform().GetLocation();
			FVector AimLocation(TargetPos.X, TargetPos.Y, Location.Z);
			float TargetRadius = 0.f;

			if (ActionDef.TargetKind == EAsceTargetKind::Unit)
			{
				if (!bTargetIsUnit)
				{
					continue;
				}
				const bool bTargetSelf = (TargetHandle == Handle);
				if (bTargetSelf != (ActionDef.TargetRelation == EAsceTargetRelation::Self))
				{
					continue;
				}
				if (!bTargetSelf && ActionDef.TargetRelation != EAsceTargetRelation::Any)
				{
					const FAsceTeamFragment* TargetTeam = EntityManager.GetFragmentDataPtr<FAsceTeamFragment>(TargetHandle);
					const int32 TargetTeamId = TargetTeam ? AsceTeamId(TargetTeam->TeamKey) : AsceNeutralTeamId;
					const bool bFriendly = (MyTeamId != AsceNeutralTeamId) && (TargetTeamId == MyTeamId);
					if (ActionDef.TargetRelation == EAsceTargetRelation::Enemy && bFriendly)
					{
						continue;
					}
					if (ActionDef.TargetRelation == EAsceTargetRelation::Friendly && !bFriendly)
					{
						continue;
					}
				}
				if (const FTransformFragment* TargetTransform = EntityManager.GetFragmentDataPtr<FTransformFragment>(TargetHandle))
				{
					AimLocation = TargetTransform->GetTransform().GetLocation();
				}
				if (const FAgentRadiusFragment* TargetRadiusFragment = EntityManager.GetFragmentDataPtr<FAgentRadiusFragment>(TargetHandle))
				{
					TargetRadius = TargetRadiusFragment->Radius;
				}
			}
			else if (ActionDef.TargetKind == EAsceTargetKind::None)
			{
				AimLocation = Location;
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

			// ── ④ 通过 ⇒ 预测表现（写客户端表现 fragment；不扣蓝、不落 CD）──
			FAsceCombatVisualFragment* Visual = EntityManager.GetFragmentDataPtr<FAsceCombatVisualFragment>(Handle);
			if (!Visual)
			{
				continue;
			}
			const float FacingYaw = FMath::RadiansToDegrees(FMath::Atan2(AimLocation.Y - Location.Y, AimLocation.X - Location.X));
			Visual->ActionState = EAsceActionState::Casting;
			Visual->ActionDefId = Slots->Slots[SlotId].ActionId;
			Visual->Counter = Visual->Counter + 1;   // 变了 ⇒ 表现层"播一次"
			Visual->PlayRate = 1.f;                  // 技能 v1 = 1.0（与服务器一致）
			Visual->FacingYaw = FacingYaw;           // 客户端转身 processor 据此限速转过去
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

	// 本地预测默认关闭：客户端不跑移动模拟/碰撞，位置完全由服务器同步驱动（见架构决策）。
	// 要恢复"点击立刻动"时打开 bEnableClientPrediction，并配套处理 ack 对齐与预测期碰撞抑制。
	if (!bEnableClientPrediction)
	{
		return;
	}

	// Standalone/ListenServer下Owner本身就是Authority：上面这行RPC调用会在本地同步直接跑到_Implementation，
	// 这里如果也执行一遍会让PathPlan被多Acquire一次，造成引用计数泄漏，所以要用HasAuthority()挡住。
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

	// 记下这次本地预测实际生效的 entity，供后续拒绝回执反查（超过容量丢最旧一条）
	RecordPendingLocalOrder(PendingLocalOrders, PendingLocalOrdersCapacity, OrderSequence, MoveTemp(ValidHandles));
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

		// 施法中（Casting/Channel）不能被移动打断（设计 §4：施法期间单位静止）⇒ 本批静默跳过该单位。
		// 之后做"玩家可配置是否允许移动打断施法"时，这里按配置分支（统一设置功能时再加）。
		if (const FAsceActionFragment* Action = EntityManager.GetFragmentDataPtr<FAsceActionFragment>(Handle))
		{
			if (Action->ActionState == EAsceActionState::Casting || Action->ActionState == EAsceActionState::Channel)
			{
				continue;
			}
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
// RequestSkillOrder / ServerRequestSkillOrder（释放技能）
// ──────────────────────────────────────────

void UAsceOrderChannelComponent::RequestSkillOrder(const TArray<FMassEntityHandle>& LocalSelectedHandles, int32 SlotId, FVector2D TargetPos, FMassNetworkID TargetNetID, FMassEntityHandle TargetLocalHandle)
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
	TArray<FMassEntityHandle> ValidHandles;
	NetIDs.Reserve(LocalSelectedHandles.Num());
	ValidHandles.Reserve(LocalSelectedHandles.Num());
	for (const FMassEntityHandle& Handle : LocalSelectedHandles)
	{
		if (!EntityManager.IsEntityValid(Handle))
		{
			continue;
		}
		if (const FMassNetworkIDFragment* NetIDFragment = EntityManager.GetFragmentDataPtr<FMassNetworkIDFragment>(Handle))
		{
			NetIDs.Add(NetIDFragment->NetID);
			ValidHandles.Add(Handle);
		}
	}
	if (NetIDs.Num() == 0)
	{
		return;
	}

	// 纯客户端才做预测（standalone / 听服 host 自己就是权威端，表现来自本地权威态，不需要预测）
	if (World->GetNetMode() == NM_Client)
	{
		const AAsceGameState* GameState = World->GetGameState<AAsceGameState>();
		PredictSkillOrderLocally(EntityManager, ValidHandles, SlotId, TargetPos, TargetLocalHandle
			, GetRequesterTeamId(), GameState ? GameState->GetServerWorldTimeSeconds() : 0.0);

		// 记下本次下单的本地 entity：拒绝回执（`ClientRejectSkillOrder`）按 OrderSequence 反查它们
		RecordPendingLocalOrder(PendingLocalOrders, PendingLocalOrdersCapacity, OrderSequence, MoveTemp(ValidHandles));
	}

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
	// 可见性兜底用的迷雾键（与战斗扫描同一套：TeamId → 本端打包 TeamKey）
	const UAsceFogSubsystem* Fog = World->GetSubsystem<UAsceFogSubsystem>();
	const int32 FogKey = GameState ? GameState->GetTeamKey(RequesterTeamId) : 0;

	// 指向性技能的目标（客户端只能点到"已复制到自己这里"的单位 ⇒ 天然不会隔雾点人；服务器照样兜底校验）
	FMassEntityHandle TargetHandle;
	if (TargetNetID.IsValid())
	{
		TargetHandle = Registry->FindEntity(TargetNetID);
	}

	// 逐单位被拒明细：**一次请求一条 RPC** 在循环后统一回传（与 `ServerRequestSkillOrder` 对称）
	TArray<FAsceEntityReject> RejectedUnits;

	for (const FMassNetworkID& NetID : SelectedNetIDs)
	{
		const FMassEntityHandle Handle = Registry->FindEntity(NetID);
		if (!EntityManager.IsEntityValid(Handle))
		{
			RejectedUnits.Add({ NetID, EAsceOrderRejectReason::EntityNotFound });
			continue;
		}
		const FAsceTeamFragment* TeamFragment = EntityManager.GetFragmentDataPtr<FAsceTeamFragment>(Handle);
		if (!TeamFragment || AsceTeamId(TeamFragment->TeamKey) != RequesterTeamId)
		{
			RejectedUnits.Add({ NetID, EAsceOrderRejectReason::NotOwned });
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
			RejectedUnits.Add({ NetID, EAsceOrderRejectReason::SkillSlotEmpty });
			continue;
		}
		FAsceActionDef ActionDef;
		if (!FAsceActionDefLibrary::GetActionDef(Slots->Slots[SlotId].ActionId, ActionDef)
			|| !ActionDef.CastConfig.IsValid())
		{
			RejectedUnits.Add({ NetID, EAsceOrderRejectReason::SkillCasterInvalid });   // 技能表查不到 / 没有动作管线配置
			continue;
		}
		if (Slots->Slots[SlotId].NextReadyServerTime > Now)
		{
			RejectedUnits.Add({ NetID, EAsceOrderRejectReason::SkillOnCooldown });
			continue;
		}
		if (const FAsceManaFragment* Mana = EntityManager.GetFragmentDataPtr<FAsceManaFragment>(Handle))
		{
			if (Mana->CurrentMana < ActionDef.ManaCost)
			{
				RejectedUnits.Add({ NetID, EAsceOrderRejectReason::SkillNotEnoughMana });
				continue;
			}
		}
		FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Handle);
		if (!Transform)
		{
			RejectedUnits.Add({ NetID, EAsceOrderRejectReason::SkillCasterInvalid });
			continue;
		}
		const FVector Location = Transform->GetTransform().GetLocation();

		FVector AimLocation(TargetPos.X, TargetPos.Y, Location.Z);
		if (ActionDef.TargetKind == EAsceTargetKind::Unit)
		{
			// 指向性：没点到有效单位 ⇒ 什么都不发生（只停）
			if (!TargetHandle.IsValid() || !EntityManager.IsEntityValid(TargetHandle))
			{
				RejectedUnits.Add({ NetID, EAsceOrderRejectReason::SkillTargetInvalid });
				continue;
			}
			const bool bTargetSelf = (TargetHandle == Handle);
			if (bTargetSelf != (ActionDef.TargetRelation == EAsceTargetRelation::Self))
			{
				// Self 技能只能点自己；非 Self 技能不能点自己
				RejectedUnits.Add({ NetID, EAsceOrderRejectReason::SkillTargetInvalid });
				continue;
			}
			if (!bTargetSelf && ActionDef.TargetRelation != EAsceTargetRelation::Any)
			{
				// 目标关系（Enemy = 非同队含中立；Friendly = 同队含自己）
				const FAsceTeamFragment* TargetTeam = EntityManager.GetFragmentDataPtr<FAsceTeamFragment>(TargetHandle);
				const int32 TargetTeamId = TargetTeam ? AsceTeamId(TargetTeam->TeamKey) : AsceNeutralTeamId;
				const bool bFriendly = (RequesterTeamId != AsceNeutralTeamId) && (TargetTeamId == RequesterTeamId);
				if ((ActionDef.TargetRelation == EAsceTargetRelation::Enemy && bFriendly)
					|| (ActionDef.TargetRelation == EAsceTargetRelation::Friendly && !bFriendly))
				{
					RejectedUnits.Add({ NetID, EAsceOrderRejectReason::SkillTargetInvalid });
					continue;
				}
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
			RejectedUnits.Add({ NetID, EAsceOrderRejectReason::SkillOutOfRange });
			continue;
		}

		// 可见性兜底：不对雾内目标/落点施法（客户端只能点到已复制的单位，这里是防篡改客户端的服务器校验；
		// 与战斗扫描同口径；`None`（自身/周围）不需要检查，放完由目标扫描各自校验）
		if (Fog && Fog->IsReady() && ActionDef.TargetKind != EAsceTargetKind::None
			&& !Fog->IsCellVisibleAtWorld(FogKey, FVector2D(AimLocation.X, AimLocation.Y)))
		{
			RejectedUnits.Add({ NetID, EAsceOrderRejectReason::SkillTargetNotVisible });
			continue;   // 不可见：只停不放
		}

		// ── ③ 进动作（技能优先级最高：可顶掉攻击与施法；Trigger 前被打断 ⇒ 不结算，Trigger 后 ⇒ 保留）──
		// 施法推进（Trigger：扣蓝/落 CD/effect 入队；持续施法进 Channel 后周期结算）在 `UAsceCombatProcessor` 里。
		FAsceActionFragment* Action = EntityManager.GetFragmentDataPtr<FAsceActionFragment>(Handle);
		if (!Action)
		{
			RejectedUnits.Add({ NetID, EAsceOrderRejectReason::SkillCasterInvalid });
			continue;
		}
		const FAsceSkillCastConfig* CastConfig = ActionDef.CastConfig.GetPtr<FAsceSkillCastConfig>();
		const bool bChannel = (CastConfig && CastConfig->IsChannel());
		const float TriggerDelay = FMath::Max(CastConfig ? CastConfig->GetTriggerTime() : 0.f, 0.f);
		const float TotalTime = FMath::Max(CastConfig ? CastConfig->GetTotalCastTime() : 0.f, 0.f);

		const float FacingYaw = FMath::RadiansToDegrees(FMath::Atan2(AimLocation.Y - Location.Y, AimLocation.X - Location.X));
		Action->ActionState = EAsceActionState::Casting;   // 持续施法也先走前摇
		Action->ActionDefId = Slots->Slots[SlotId].ActionId;
		Action->ActionTarget = TargetHandle;
		Action->ActionTargetPos = FVector2D(AimLocation.X, AimLocation.Y);
		Action->FacingYaw = FacingYaw;
		Action->TriggerTime = Now + TriggerDelay;
		Action->ActionEndTime = Now + TotalTime;
		Action->bTriggered = false;
		Action->NextTickTime = 0.0;   // 进入 Channel 时才设
		++Action->Counter;

		// cost / CD 在 **Trigger** 那一刻处理（见 `Combat/技能系统设计.md` §5）——这里不付费。

		// 动作开始时对齐朝向（服务器权威朝向；客户端表现靠动作态通道的 FacingQ）
		Transform->GetMutableTransform().SetRotation(FRotator(0.f, FacingYaw, 0.f).Quaternion());

		UE_LOG(LogTemp, Verbose, TEXT("[Skill] %lld 开始施法：技能 %d（槽 %d，%s）"),
			static_cast<int64>(Handle.AsNumber()), Action->ActionDefId, SlotId, bChannel ? TEXT("Channel") : TEXT("NormalCast"));
	}

	// 一次请求一条 RPC 把逐单位拒绝明细回传（空数组不发）——客户端据此撤销那些单位的预测表现
	if (RejectedUnits.Num() > 0)
	{
		ClientRejectSkillOrder(OrderSequence, RejectedUnits);
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
	UE_LOG(LogTemp, Warning, TEXT("[Order] Client 收到整批拒绝：seq=%d, reason=%d"), OrderSequence, static_cast<int32>(Reason));

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
	UE_LOG(LogTemp, Warning, TEXT("[Order] Client 收到单实体拒绝：seq=%d, reason=%d"), OrderSequence, static_cast<int32>(Reason));

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

void UAsceOrderChannelComponent::ClientRejectSkillOrder_Implementation(int32 OrderSequence, const TArray<FAsceEntityReject>& RejectedUnits)
{
	UE_LOG(LogTemp, Verbose, TEXT("[Order] Client 收到技能拒绝：seq=%d, %d 个单位"), OrderSequence, RejectedUnits.Num());

	// 按 OrderSequence 反查本次下单涉及的本地 entity；查不到（不是我们预测过的 / 已被挤出容量）⇒ 忽略
	TArray<FMassEntityHandle>* HandlesPtr = PendingLocalOrders.Find(OrderSequence);
	if (!HandlesPtr)
	{
		return;
	}

	const UWorld* World = GetWorld();
	UMassEntitySubsystem* MassEntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!MassEntitySubsystem)
	{
		return;
	}
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();

	// 逐条：在这批本地 entity 里按 NetID 找到被拒的那个 ⇒ 撤销它的预测表现（回 Idle）。
	// 不在 RejectedUnits 里的 = 服务器接受了 ⇒ 等动作态边沿当隐式确认（会整段覆写视觉 fragment）。
	for (const FAsceEntityReject& Reject : RejectedUnits)
	{
		for (int32 Index = 0; Index < HandlesPtr->Num(); ++Index)
		{
			const FMassEntityHandle Handle = (*HandlesPtr)[Index];
			if (!EntityManager.IsEntityValid(Handle))
			{
				continue;
			}
			const FMassNetworkIDFragment* NetIDFragment = EntityManager.GetFragmentDataPtr<FMassNetworkIDFragment>(Handle);
			if (!NetIDFragment || !(NetIDFragment->NetID == Reject.NetID))
			{
				continue;
			}

			// 撤销预测：回 Idle（`Counter++` 让"变了就播一次"生效）。若该单位其实还在攻击/施法（服务器不因
			// 技能被拒就取消当前动作），下一次权威动作态边沿会把它纠回来。
			if (FAsceCombatVisualFragment* Visual = EntityManager.GetFragmentDataPtr<FAsceCombatVisualFragment>(Handle))
			{
				Visual->ActionState = EAsceActionState::Idle;
				Visual->Counter = Visual->Counter + 1;
			}

			HandlesPtr->RemoveAtSwap(Index);
			break;
		}
	}

	if (HandlesPtr->Num() == 0)
	{
		PendingLocalOrders.Remove(OrderSequence);
	}
}
