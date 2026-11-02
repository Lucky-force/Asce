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
#include "AsceMass/Lifecycle/AsceDestroyQueueSubsystem.h" // 死亡入队（帧首限流 drain）
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Fragments/AsceOrderFragments.h"
#include "AsceMass/SpatialGrid/AsceSpatialGridSubsystem.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"
#include "GameModes/AsceGameState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceCombatProcessor)

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
	EntityQuery.AddRequirement<FAsceCombatFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
	// 攻击属性（射程 / 攻击动作行）在属性 fragment 里；运行时 fragment 只放目标与扫描节流
	EntityQuery.AddRequirement<FAsceCombatAttributesFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery.AddRequirement<FAsceActionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FAsceTeamFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);   // 动作开始时要写朝向
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
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
		const int32 Num = Ctx.GetNumEntities();
		const TArrayView<FAsceCombatFragment> Combats = Ctx.GetMutableFragmentView<FAsceCombatFragment>();
		const bool bHasCombat = Combats.Num() == Num;
		const TConstArrayView<FAsceCombatAttributesFragment> CombatAttrs = Ctx.GetFragmentView<FAsceCombatAttributesFragment>();
		const bool bHasCombatAttrs = CombatAttrs.Num() == Num;
		const TArrayView<FAsceActionFragment> Actions = Ctx.GetMutableFragmentView<FAsceActionFragment>();
		const TArrayView<FTransformFragment> Transforms = Ctx.GetMutableFragmentView<FTransformFragment>();
		const TConstArrayView<FAsceTeamFragment> Teams = Ctx.GetFragmentView<FAsceTeamFragment>();
		const TConstArrayView<FAgentRadiusFragment> Radii = Ctx.GetFragmentView<FAgentRadiusFragment>();
		const bool bHasRadius = Radii.Num() == Num;
		const TArrayView<FAsceHealthFragment> Healths = Ctx.GetMutableFragmentView<FAsceHealthFragment>();
		const TConstArrayView<FAsceMoveOrderFragment> MoveOrders = Ctx.GetFragmentView<FAsceMoveOrderFragment>();
		const TArrayView<FAsceLifecycleFragment> Lifecycles = Ctx.GetMutableFragmentView<FAsceLifecycleFragment>();
		const TConstArrayView<FMassEntityHandle> Entities = Ctx.GetEntities();

		TArray<FMassEntityHandle> Candidates;   // 复用（每个 chunk 一次分配）

		for (int32 i = 0; i < Num; ++i)
		{
			FAsceCombatFragment* Combat = bHasCombat ? &Combats[i] : nullptr;
			FAsceActionFragment& Action = Actions[i];

			const FMassEntityHandle Self = Entities[i];
			const FVector Location = Transforms[i].GetTransform().GetLocation();
			const float Radius = bHasRadius ? Radii[i].Radius : 0.f;
			const int32 MyTeamId = AsceTeamId(Teams[i].TeamKey);
			// 移动中（有移动命令）⇒ 不扫描、且攻击会被打断（打断不结算）
			const bool bMoving = MoveOrders.Num() > 0 && MoveOrders[i].bHasOrder;

			// 已死亡（等销毁队列处理）：停一切行动；动画表现由客户端本地死亡特效负责
			if (Lifecycles.Num() == Num && Lifecycles[i].IsDying())
			{
				continue;
			}

			// 施法（Casting）与攻击（Attack）都已由**行为树**驱动（`Asce Run Ability Tree` + 子树）。
			// 本 processor 只保留：自动攻击的**扫描/起手**（写动作态 + 目标 + 锚点/Counter）和移动打断。

			// 攻击可被移动打断（技能不行）：只做状态取消，攻击子树由树的 `Action.Idle` 转移停掉
			if (Action.ActionState == EAsceActionState::Attack && bMoving)
			{
				Action.ActionState = EAsceActionState::Idle;
				Action.ActionTarget.Reset();
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
			if (Action.ActionState != EAsceActionState::Idle || bMoving)
			{
				continue;
			}
			if (!Combat || !bHasCombatAttrs)
			{
				continue;
			}
			const FAsceCombatAttributesFragment& CombatAttr = CombatAttrs[i];
			// 没有攻击动作行 = 这个单位不能主动攻击（纯施法/模块未配攻击），不要进入 Attack
			if (CombatAttr.AttackActionId <= 0)
			{
				continue;
			}
			if (Now < Combat->NextScanTime)
			{
				continue;
			}
			Combat->NextScanTime = Now + ScanIntervalSeconds;

			// 视线用的迷雾键：本端阵营表把 TeamId 映射成打包键（与迷雾贡献同一套）
			const int32 FogKey = GameState ? GameState->GetTeamKey(MyTeamId) : Teams[i].TeamKey;

			// 目标选择：指定目标优先（范围内优先），否则射程内最近的敌方
			FMassEntityHandle Target;
			float BestDistSq = TNumericLimits<float>::Max();

			TArray<FMassEntityHandle> SearchList;
			if (Combat->DesignatedTarget.IsValid() && EntityManager.IsEntityValid(Combat->DesignatedTarget))
			{
				SearchList.Add(Combat->DesignatedTarget);
			}
			else
			{
				Combat->DesignatedTarget.Reset();
				Candidates.Reset();
				Grid->FindEntities(FVector2D(Location.X, Location.Y), CombatAttr.AttackRange + Radius, Candidates);
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
				if (Dist2D > CombatAttr.AttackRange + Radius + CandidateRadius)
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
			Action.ActionDefId = CombatAttr.AttackActionId;
			Action.ActionTarget = Target;
			Action.ActionTargetPos = FVector2D(TargetLocation.X, TargetLocation.Y);
			Action.FacingYaw = FacingYaw;
			++Action.Counter;

			Transforms[i].GetMutableTransform().SetRotation(FRotator(0.f, FacingYaw, 0.f).Quaternion());

			UE_LOG(LogTemp, Verbose, TEXT("[Combat] %lld 开始攻击 %lld（攻击行 %d）"),
				static_cast<int64>(Self.AsNumber()), static_cast<int64>(Target.AsNumber()), Action.ActionDefId);
		}
	});
}
