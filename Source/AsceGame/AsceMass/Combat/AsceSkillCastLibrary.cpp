// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Combat/AsceSkillCastLibrary.h"

#include "AsceMass/Behavior/AsceTimelineLibrary.h"
#include "AsceMass/Combat/AsceActionDefLibrary.h"

#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"

#include "AsceMass/Combat/AsceCombatDef.h"
#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Fragments/AsceOrderFragments.h"
#include "AsceMass/Lifecycle/AsceDestroyQueueSubsystem.h"
#include "AsceMass/Spawn/AsceSpawnQueueSubsystem.h"

namespace AsceSkillCast
{
	float ApplyDamage(FMassEntityManager& EntityManager, UWorld* World, const FMassEntityHandle Victim, const float Amount)
	{
		if (!EntityManager.IsEntityValid(Victim))
		{
			return -1.f;
		}
		FAsceHealthFragment* VictimHealth = EntityManager.GetFragmentDataPtr<FAsceHealthFragment>(Victim);
		if (!VictimHealth)
		{
			return -1.f;
		}

		VictimHealth->CurrentHealth = FMath::Max(0.f, VictimHealth->CurrentHealth - Amount);
		if (VictimHealth->CurrentHealth > 0.f)
		{
			return VictimHealth->CurrentHealth;
		}

		// 死亡：置 Dying（别处据此跳过）+ 停行动 + 入销毁队列（帧首限流 drain）。
		// 只在服务器做（客户端由动作态/死亡 RPC 驱动表现）。
		FAsceLifecycleFragment* VictimLifecycle = EntityManager.GetFragmentDataPtr<FAsceLifecycleFragment>(Victim);
		if (!VictimLifecycle || VictimLifecycle->IsDying())
		{
			return 0.f;   // 已经在死亡流程里（幂等）
		}
		VictimLifecycle->SetDying(true);

		if (FAsceMoveOrderFragment* VictimMoveOrder = EntityManager.GetFragmentDataPtr<FAsceMoveOrderFragment>(Victim))
		{
			VictimMoveOrder->bHasOrder = false;
		}
		if (FAsceActionFragment* VictimAction = EntityManager.GetFragmentDataPtr<FAsceActionFragment>(Victim))
		{
			VictimAction->ActionState = EAsceActionState::Idle;
			VictimAction->ActionTarget.Reset();
			++VictimAction->Counter;
		}
		if (UAsceDestroyQueueSubsystem* DestroyQueue = World ? World->GetSubsystem<UAsceDestroyQueueSubsystem>() : nullptr)
		{
			DestroyQueue->Enqueue(Victim);
		}
		UE_LOG(LogTemp, Verbose, TEXT("[Death] %lld HP<=0 ⇒ 入销毁队列"), static_cast<int64>(Victim.AsNumber()));
		return 0.f;
	}

	bool PayCostAndCooldown(FMassEntityManager& EntityManager, const FMassEntityHandle Caster
		, const int32 ActionId, const FAsceActionDef& Def, const double Now)
	{
		if (FAsceManaFragment* Mana = EntityManager.GetFragmentDataPtr<FAsceManaFragment>(Caster))
		{
			if (Mana->CurrentMana < Def.ManaCost)
			{
				return false;   // 蓝不够：不扣、不落 CD、不生成
			}
			Mana->CurrentMana = FMath::Max(0.f, Mana->CurrentMana - Def.ManaCost);
		}

		if (FAsceSkillSlotsFragment* Slots = EntityManager.GetFragmentDataPtr<FAsceSkillSlotsFragment>(Caster))
		{
			// 技能 CD 记在槽位上（换槽 = 技能连同 CD 一起换位）；按 ActionId 找到当前所在槽位
			for (int32 SlotIndex = 0; SlotIndex < FAsceSkillSlotsFragment::NumSkillSlots; ++SlotIndex)
			{
				if (Slots->Slots[SlotIndex].ActionId == ActionId)
				{
					Slots->Slots[SlotIndex].NextReadyServerTime = Now + Def.Cooldown;
					break;
				}
			}
		}
		return true;
	}

	void ApplyEffect(FMassEntityManager& EntityManager, UWorld* World, const FMassEntityHandle Caster
		, const FMassEntityHandle Target, const FVector2D& TargetPos, const float FacingYaw
		, const int32 CasterTeamId, const FAsceActionDef& Def)
	{
		// 指向技能但目标没了 ⇒ 什么都不做（不生成、不补刀）
		if (Def.TargetKind == EAsceTargetKind::Unit
			&& (!Target.IsValid() || !EntityManager.IsEntityValid(Target)))
		{
			return;
		}

		if (Def.Delivery != EAsceDelivery::Instant)
		{
			// 生成物：**结算只入队**，实体在帧末（阶段 8）统一创建（见 `UAsceSpawnQueueSubsystem`）
			UAsceSpawnQueueSubsystem* SpawnQueue = World ? World->GetSubsystem<UAsceSpawnQueueSubsystem>() : nullptr;
			if (!SpawnQueue)
			{
				return;
			}
			const FTransformFragment* CasterTransform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Caster);
			const FVector CasterLocation = CasterTransform ? CasterTransform->GetTransform().GetLocation() : FVector::ZeroVector;

			FAsceSpawnRequest Request;
			Request.TeamId = CasterTeamId;   // 生成物沿用发射者阵营（跨端一致的 TeamId）
			if (Def.Delivery == EAsceDelivery::Projectile)
			{
				// 弹道：施法者**前方**生成（朝向 = 本次动作定死的 FacingYaw）
				const FRotator FacingRotation(0.f, FacingYaw, 0.f);
				const FVector Forward = FRotationMatrix(FacingRotation).GetUnitAxis(EAxis::X);
				Request.DefRowName = Def.ProjectileDefRow;
				Request.Transform = FTransform(FacingRotation, CasterLocation + Forward * 100.f);
			}
			else
			{
				// `SpawnAtTarget`：在结算时的目标点生成
				Request.DefRowName = Def.SpawnGroupDefRow;
				Request.Transform = FTransform(FRotator::ZeroRotator, FVector(TargetPos.X, TargetPos.Y, CasterLocation.Z));
			}
			SpawnQueue->Enqueue(Request);
			// TODO(弹道追踪)：`Projectile` 的"追着目标直到命中/目标死即消失"需要弹道自己的 fragment + processor（未做）
			return;
		}

		// 直接伤害：动作行 `Damage == 0` ⇒ 跟随施法者的**单位攻击力**（基础属性；与工厂对攻击行的解析一致）
		const FAsceCombatAttributesFragment* CasterCombat = EntityManager.GetFragmentDataPtr<FAsceCombatAttributesFragment>(Caster);
		const float Damage = (Def.Damage > 0.f) ? Def.Damage : (CasterCombat ? CasterCombat->AttackDamage : 0.f);
		if (Target.IsValid())
		{
			ApplyDamage(EntityManager, World, Target, Damage);
		}
	}

	bool SettleFromAction(FMassEntityManager& EntityManager, UWorld* World, const FMassEntityHandle Caster
		, const FAsceActionFragment& Action, const int32 CasterTeamId, const bool bPayCost)
	{
		// 逻辑只在服务器：客户端直接"通过"，让表现侧的树照常往下走
		if (!World || World->GetNetMode() == NM_Client)
		{
			return true;
		}
		// 施法者已死亡：不再结算（树也不再被 tick/发事件）
		if (const FAsceLifecycleFragment* CasterLifecycle = EntityManager.GetFragmentDataPtr<FAsceLifecycleFragment>(Caster))
		{
			if (CasterLifecycle->IsDying())
			{
				return false;
			}
		}
		FAsceActionDef Def;
		if (!FAsceActionDefLibrary::GetActionDef(Action.ActionDefId, Def))
		{
			return false;   // 技能表查不到（配置错误）
		}
		if (bPayCost
			&& !PayCostAndCooldown(EntityManager, Caster, Action.ActionDefId, Def, AsceTimeline::GetServerNow(*World)))
		{
			return false;   // 蓝不够
		}
		ApplyEffect(EntityManager, World, Caster, Action.ActionTarget, Action.ActionTargetPos, Action.FacingYaw, CasterTeamId, Def);
		return true;
	}
}