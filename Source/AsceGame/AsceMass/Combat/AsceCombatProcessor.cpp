// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceCombatProcessor.h"

#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "MassCommonFragments.h"          // FTransformFragment / FAgentRadiusFragment
#include "Engine/World.h"
#include "Math/NumericLimits.h"

#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Combat/AsceSkillCastLibrary.h"
#include "HAL/IConsoleManager.h"   // TAutoConsoleVariable（迁移开关）
#include "AsceMass/Combat/AsceActionDefLibrary.h"   // 施法推进要读技能定义
#include "AsceMass/Spawn/AsceSpawnQueueSubsystem.h" // 生成物入队（结算只入队，帧末创建）
#include "AsceMass/Lifecycle/AsceDestroyQueueSubsystem.h" // 死亡入队（帧首限流 drain）
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Fragments/AsceOrderFragments.h"
#include "AsceMass/SpatialGrid/AsceSpatialGridSubsystem.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"
#include "GameModes/AsceGameState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceCombatProcessor)

namespace
{
	/** 施法推进（Trigger / Channel / 回 Idle）是否已交给**行为树**驱动。
	 *  0（默认）= 仍由本 processor 驱动；1 = 本 processor 不再碰 Casting/Channel，改由
	 *  `AsceWaitTimelineTask` / `AsceSettleSkillTask` / `AsceSetActionStateTask` 串成的技能子树驱动。
	 *  见 `Combat/技能编辑器设计.md` §9（B3）。 */
	TAutoConsoleVariable<int32> CVarTreeDrivesCasting(
		TEXT("Asce.Combat.TreeDrivesCasting"), 0,
		TEXT("1 = 施法推进由行为树驱动（见 Combat/技能编辑器设计.md §9）；0 = 仍由 AsceCombatProcessor 驱动"),
		ECVF_Default);
}

UAsceCombatProcessor::UAsceCombatProcessor()
	: EntityQuery(*this)
{
	// 不注册到 Mass 全局调度：由 `UAsceSimulationSubsystem` 的战斗阶段按固定顺序驱动
	bAutoRegisterWithProcessingPhases = false;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
}

void UAsceCombatProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddRequirement<FAsceCombatFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FAsceActionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FAsceTeamFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);   // 动作开始时要写朝向
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	// 血量/移动命令用 Optional：没有的不参与（不能因为缺一个 fragment 就让单位从战斗查询里消失）
	EntityQuery.AddRequirement<FAsceHealthFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
	EntityQuery.AddRequirement<FAsceMoveOrderFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	// 生命周期（Dying/DestroyRequested）：死亡现场要写它（置 Dying），也要读它（跳过已死亡）
	EntityQuery.AddRequirement<FAsceLifecycleFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
}

void UAsceCombatProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UAsceSpatialGridSubsystem* Grid = World->GetSubsystem<UAsceSpatialGridSubsystem>();
	UAsceFogSubsystem* Fog = World->GetSubsystem<UAsceFogSubsystem>();
	const AAsceGameState* GameState = World->GetGameState<AAsceGameState>();
	if (!Grid)
	{
		return;
	}

	// 权威时钟：用复制给客户端的那条服务器时间轴，保证槽位/CD 时间戳两端同源
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const TArrayView<FAsceCombatFragment> Combats = Ctx.GetMutableFragmentView<FAsceCombatFragment>();
		const TArrayView<FAsceActionFragment> Actions = Ctx.GetMutableFragmentView<FAsceActionFragment>();
		const TArrayView<FTransformFragment> Transforms = Ctx.GetMutableFragmentView<FTransformFragment>();
		const TConstArrayView<FAsceTeamFragment> Teams = Ctx.GetFragmentView<FAsceTeamFragment>();
		const TConstArrayView<FAgentRadiusFragment> Radii = Ctx.GetFragmentView<FAgentRadiusFragment>();
		const TArrayView<FAsceHealthFragment> Healths = Ctx.GetMutableFragmentView<FAsceHealthFragment>();
		const TConstArrayView<FAsceMoveOrderFragment> MoveOrders = Ctx.GetFragmentView<FAsceMoveOrderFragment>();
		const TArrayView<FAsceLifecycleFragment> Lifecycles = Ctx.GetMutableFragmentView<FAsceLifecycleFragment>();
		const TConstArrayView<FMassEntityHandle> Entities = Ctx.GetEntities();
		const int32 Num = Ctx.GetNumEntities();

		TArray<FMassEntityHandle> Candidates;   // 复用（每个 chunk 一次分配）

		for (int32 i = 0; i < Num; ++i)
		{
			FAsceCombatFragment& Combat = Combats[i];
			FAsceActionFragment& Action = Actions[i];

			const FMassEntityHandle Self = Entities[i];
			const FVector Location = Transforms[i].GetTransform().GetLocation();
			const float Radius = Radii[i].Radius;
			const int32 MyTeamId = AsceTeamId(Teams[i].TeamKey);
			// 移动中（有移动命令）⇒ 不扫描、且攻击会被打断（打断不结算）
			const bool bMoving = MoveOrders.Num() > 0 && MoveOrders[i].bHasOrder;

			// 已死亡（等销毁队列处理）：停一切行动；动画表现由客户端本地死亡特效负责
			if (Lifecycles.Num() == Num && Lifecycles[i].IsDying())
			{
				continue;
			}

			// 播放速度：AttackSpeed 180 → 1.0x（见 `技能系统设计.md`）
			const float PlayRate = FMath::Max(Combat.AttackSpeed / 200.f + 0.1f, KINDA_SMALL_NUMBER);

			// ───────── ①a 施法推进：Trigger 结算（普通=TriggerTime；持续=前摇结束进 Channel）─────────
			if (Action.ActionState == EAsceActionState::Casting || Action.ActionState == EAsceActionState::Channel)
			{
				// 迁移开关：施法推进已交给行为树 ⇒ 本 processor 不再碰（避免和树重复结算）
				if (CVarTreeDrivesCasting.GetValueOnGameThread() != 0)
				{
					continue;
				}

				FAsceActionDef ActionDef;
				if (!FAsceActionDefLibrary::GetActionDef(Action.ActionDefId, ActionDef))
				{
					// 技能表查不到（配置错误）：不让单位卡在施法态
					Action.ActionState = EAsceActionState::Idle;
					Action.ActionTarget.Reset();
					++Action.Counter;
					continue;
				}

				const FAsceSkillCastConfig* CastConfig = ActionDef.CastConfig.GetPtr<FAsceSkillCastConfig>();

				// 单次结算：交给共享 API（`AsceSkillCast::ApplyEffect`）——直接伤害（含死亡判定）或生成物入队
				auto SettleOnce = [&]()
				{
					AsceSkillCast::ApplyEffect(EntityManager, GetWorld(), Self, Action.ActionTarget
						, Action.ActionTargetPos, Action.FacingYaw, AsceTeamId(Teams[i].TeamKey), ActionDef);
				};
				// `Channel`：周期结算（cost/CD 已在"真正释放"那一刻付过）
				if (Action.ActionState == EAsceActionState::Channel)
				{
					if (Now >= Action.NextTickTime)
					{
						SettleOnce();
						Action.NextTickTime = Now + FMath::Max(CastConfig ? CastConfig->GetTickInterval() : 0.5f, 0.01f);
					}
					if (Now >= Action.ActionEndTime)
					{
						Action.ActionState = EAsceActionState::Idle;
						Action.ActionTarget.Reset();
						++Action.Counter;
					}
					continue;
				}

				// 已经 Trigger 过：只剩动画尾巴（普通施法）
				if (Action.bTriggered)
				{
					if (Now >= Action.ActionEndTime)
					{
						Action.ActionState = EAsceActionState::Idle;
						Action.ActionTarget.Reset();
						++Action.Counter;
					}
					continue;
				}

				// `Casting`：**Trigger 那一刻才是真正释放** —— 先 cost（扣蓝 + 写槽位 CD），
				// 再查一次"目标是否还存在"，最后才结算 effect（见 `技能系统设计.md` §5）
				if (Now < Action.TriggerTime)
				{
					continue;   // 还在前摇，只是播动画
				}

				// Trigger 时刻：扣蓝 + 写槽位 CD（共享 API）。蓝不够 ⇒ 释放失败：不扣、不落 CD、不生成
				if (!AsceSkillCast::PayCostAndCooldown(EntityManager, Self, Action.ActionDefId, ActionDef, Now))
				{
					Action.ActionState = EAsceActionState::Idle;
					Action.ActionTarget.Reset();
					++Action.Counter;
					continue;
				}
				SettleOnce();
				Action.bTriggered = true;

				if (CastConfig && CastConfig->IsChannel())
				{
					// 持续施法：进入 Channel（Trigger 的这次就是"首次结算"），循环动画 + 周期结算
					Action.ActionState = EAsceActionState::Channel;
					Action.ActionEndTime = Now + FMath::Max(CastConfig->GetChannelDuration(), 0.f);
					Action.NextTickTime = Now + FMath::Max(CastConfig->GetTickInterval(), 0.01f);
					++Action.Counter;
					continue;
				}

				// 普通施法：动作继续播到尾巴 → Idle
				if (Now >= Action.ActionEndTime)
				{
					Action.ActionState = EAsceActionState::Idle;
					Action.ActionTarget.Reset();
					++Action.Counter;
				}
				continue;
			}

			// ───────── ① 动作推进：攻击 —— Trigger 结算；到点回 Idle ─────────
			if (Action.ActionState == EAsceActionState::Attack)
			{
				if (bMoving)
				{
					// 移动打断：Trigger 前 ⇒ **不结算**；Trigger 后 ⇒ 已结算，直接结束
					Action.ActionState = EAsceActionState::Idle;
					Action.ActionTarget.Reset();
					++Action.Counter;
					continue;
				}

				// Trigger：结算一次（目标必须仍有效，且落在 2× 射程内）
				if (!Action.bTriggered && Now >= Action.TriggerTime)
				{
					bool bApplied = false;
					if (Action.ActionTarget.IsValid() && EntityManager.IsEntityValid(Action.ActionTarget))
					{
						if (const FTransformFragment* TargetTransform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Action.ActionTarget))
						{
							const FVector TargetLocation = TargetTransform->GetTransform().GetLocation();
							// v1：圆心距离 + 双方半径（后续替换为表面距离/高低口径）
							float TargetRadius = 0.f;
							if (const FAgentRadiusFragment* TargetRadiusFragment = EntityManager.GetFragmentDataPtr<FAgentRadiusFragment>(Action.ActionTarget))
							{
								TargetRadius = TargetRadiusFragment->Radius;
							}
							const float MaxDistance = Combat.AttackRange * 2.f + Radius + TargetRadius;
							if (FVector::Dist2D(Location, TargetLocation) <= MaxDistance)
							{
								if (Healths.Num() > 0)
								{
									if (FAsceHealthFragment* TargetHealth = EntityManager.GetFragmentDataPtr<FAsceHealthFragment>(Action.ActionTarget))
									{
										AsceSkillCast::ApplyDamage(EntityManager, GetWorld(), Action.ActionTarget, Combat.AttackDamage);
										/* 死亡判定已在 ApplyDamage 内部处理 */
										bApplied = true;
										UE_LOG(LogTemp, Verbose, TEXT("[Combat] 结算：%lld → %lld 伤害 %.1f（剩余 HP %.1f）"),
											static_cast<int64>(Self.AsNumber()), static_cast<int64>(Action.ActionTarget.AsNumber()),
											Combat.AttackDamage, TargetHealth->CurrentHealth);
									}
								}
							}
						}
					}
					if (!bApplied)
					{
						UE_LOG(LogTemp, Verbose, TEXT("[Combat] 结算失败（目标失效/超 2× 射程）：%lld"), static_cast<int64>(Self.AsNumber()));
					}
					Action.bTriggered = true;
				}

				if (Now < Action.ActionEndTime)
				{
					continue;   // 结算完还有动画尾巴
				}

				// 结束动作 → CD（从**动作结束**起算）
				Action.ActionState = EAsceActionState::Idle;
				Action.ActionTarget.Reset();
				Action.NextReadyTime = Now + 1.0 / PlayRate;
				++Action.Counter;
				continue;
			}

			// ───────── ①b Idle ↔ Move：移动也是动作态（服务器权威；客户端表现走动作态通道）─────────
			// 只在 Idle/Move 之间切，不碰其它动作；`Counter++` 让两端"变了就播一次"。
			// （到达时 `AsceArrivalProcessor` 会清 `bHasOrder` ⇒ 这里自然切回 Idle。）
			if (Action.ActionState == EAsceActionState::Idle && bMoving)
			{
				Action.ActionState = EAsceActionState::Move;
				++Action.Counter;
			}
			else if (Action.ActionState == EAsceActionState::Move && !bMoving)
			{
				Action.ActionState = EAsceActionState::Idle;
				++Action.Counter;
			}

			// ───────── ② 扫描起手（0.3s 节流）─────────
			if (Action.ActionState != EAsceActionState::Idle || !Combat.bAutoAttack || bMoving)
			{
				continue;
			}
			if (Now < Action.NextReadyTime || Now < Combat.NextScanTime)
			{
				continue;
			}
			Combat.NextScanTime = Now + ScanIntervalSeconds;

			// 视线用的迷雾键：本端阵营表把 TeamId 映射成打包键（与迷雾贡献同一套）
			const int32 FogKey = GameState ? GameState->GetTeamKey(MyTeamId) : Teams[i].TeamKey;

			// 目标选择：指定目标优先（范围内优先），否则射程内最近的敌方
			FMassEntityHandle Target;
			float BestDistSq = TNumericLimits<float>::Max();

			TArray<FMassEntityHandle> SearchList;
			if (Combat.DesignatedTarget.IsValid() && EntityManager.IsEntityValid(Combat.DesignatedTarget))
			{
				SearchList.Add(Combat.DesignatedTarget);
			}
			else
			{
				Combat.DesignatedTarget.Reset();
				Candidates.Reset();
				Grid->FindEntities(FVector2D(Location.X, Location.Y), Combat.AttackRange + Radius, Candidates);
				SearchList = Candidates;
			}

			for (const FMassEntityHandle Candidate : SearchList)
			{
				if (Candidate == Self || !EntityManager.IsEntityValid(Candidate))
				{
					continue;
				}
				const FAsceTeamFragment* CandidateTeam = EntityManager.GetFragmentDataPtr<FAsceTeamFragment>(Candidate);
				if (!CandidateTeam || AsceTeamId(CandidateTeam->TeamKey) == MyTeamId)
				{
					continue;   // 只打非己方
				}
				// 跳过已死亡（等销毁）的候选
				if (const FAsceLifecycleFragment* CandidateLifecycle = EntityManager.GetFragmentDataPtr<FAsceLifecycleFragment>(Candidate))
				{
					if (CandidateLifecycle->IsDying())
					{
						continue;
					}
				}
				const FTransformFragment* CandidateTransform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Candidate);
				if (!CandidateTransform)
				{
					continue;
				}
				const FVector CandidateLocation = CandidateTransform->GetTransform().GetLocation();

				// 射程（v1：圆心距离 + 双方半径）
				float CandidateRadius = 0.f;
				if (const FAgentRadiusFragment* CandidateRadiusFragment = EntityManager.GetFragmentDataPtr<FAgentRadiusFragment>(Candidate))
				{
					CandidateRadius = CandidateRadiusFragment->Radius;
				}
				const float Dist2D = FVector::Dist2D(Location, CandidateLocation);
				if (Dist2D > Combat.AttackRange + Radius + CandidateRadius)
				{
					continue;
				}

				// 视线：迷雾可见（不可见的目标不能打）
				if (Fog && Fog->IsReady() && !Fog->IsCellVisibleAtWorld(FogKey, FVector2D(CandidateLocation.X, CandidateLocation.Y)))
				{
					continue;
				}

				const float DistSq = FVector::DistSquared2D(Location, CandidateLocation);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					Target = Candidate;
				}
			}

			if (!Target.IsValid())
			{
				continue;
			}

			// ───────── 起手：进入 Attack（朝向立刻对齐；客户端靠动作态通道的 FacingQ 表现）─────────
			const FVector TargetLocation = EntityManager.GetFragmentDataPtr<FTransformFragment>(Target)->GetTransform().GetLocation();
			const float FacingYaw = FMath::RadiansToDegrees(FMath::Atan2(TargetLocation.Y - Location.Y, TargetLocation.X - Location.X));

			Action.ActionState = EAsceActionState::Attack;
			Action.StartServerTime = static_cast<float>(Now);   // 起轴时刻（锚点；随动作态通道下发）
			// 有"攻击动作行"就用它的 id（动画/交付走行）；0 = 旧的 AttackCastConfig 路径
			Action.ActionDefId = Combat.AttackActionId;
			Action.ActionTarget = Target;
			Action.ActionTargetPos = FVector2D(TargetLocation.X, TargetLocation.Y);
			Action.TriggerTime = Now + Combat.AttackTriggerTime / PlayRate;
			Action.ActionEndTime = Now + Combat.AttackTotalTime / PlayRate;
			Action.bTriggered = false;
			Action.FacingYaw = FacingYaw;
			++Action.Counter;

			Transforms[i].GetMutableTransform().SetRotation(FRotator(0.f, FacingYaw, 0.f).Quaternion());

			UE_LOG(LogTemp, Verbose, TEXT("[Combat] %lld 开始攻击 %lld（PlayRate %.2f，动作 %.2fs）"),
				static_cast<int64>(Self.AsNumber()), static_cast<int64>(Target.AsNumber()), PlayRate, Combat.AttackTotalTime / PlayRate);
		}
	});
}
