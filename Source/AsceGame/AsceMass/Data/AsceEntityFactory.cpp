// AsceEntityFactory.cpp
#include "AsceEntityFactory.h"

#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "AsceMass/Data/AsceUnitDef.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Fragments/AsceMovementFragments.h"
#include "AsceMass/Vision/AsceVisionFragments.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameModes/AsceGameState.h"   // 本端阵营表（TeamId → 打包 TeamKey）
#include "MassCommonFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityTemplate.h"
#include "MassSpawnerSubsystem.h"

namespace
{
	/** 把单位类型数据写进已生成的实体（有哪些 fragment 就写哪些）。 */
	void ApplyUnitDef(FMassEntityManager& EntityManager, FMassEntityHandle Entity, const FAsceRTSUnitDef& Def, int32 TeamKeyPacked)
	{
		if (FAsceHealthFragment* Health = EntityManager.GetFragmentDataPtr<FAsceHealthFragment>(Entity))
		{
			Health->MaxHealth = Def.MaxHealth;
			Health->CurrentHealth = Def.MaxHealth;
		}
		if (FAsceManaFragment* Mana = EntityManager.GetFragmentDataPtr<FAsceManaFragment>(Entity))
		{
			Mana->MaxMana = Def.MaxMana;
			Mana->CurrentMana = Def.MaxMana;
		}
		// 技能槽位：把 def 里配的初始技能填进槽位（CD 归零；换槽/施法由后续流程维护）
		if (FAsceSkillSlotsFragment* SkillSlots = EntityManager.GetFragmentDataPtr<FAsceSkillSlotsFragment>(Entity))
		{
			for (int32 SlotIndex = 0; SlotIndex < FAsceSkillSlotsFragment::NumSkillSlots; ++SlotIndex)
			{
				SkillSlots->Slots[SlotIndex].ActionId = Def.SkillSlots.IsValidIndex(SlotIndex) ? Def.SkillSlots[SlotIndex] : 0;
				SkillSlots->Slots[SlotIndex].NextReadyServerTime = 0.0;
			}
		}
		// 战斗参数（见 `Combat/技能系统设计.md`）：只写静态值；运行时状态由战斗 processor 推进
		if (FAsceCombatFragment* Combat = EntityManager.GetFragmentDataPtr<FAsceCombatFragment>(Entity))
		{
			Combat->AttackRange = Def.AttackRange;
			Combat->AttackDamage = Def.AttackDamage;
			Combat->BaseAttackTime = FMath::Max(Def.BaseAttackTime, 0.05f);
			Combat->AttackSpeed = Def.AttackSpeed;
			Combat->bAutoAttack = Def.bAutoAttackDefault;
			Combat->bProjectile = Def.bProjectile;
			Combat->ProjectileDefId = Def.ProjectileDefId;
		}
		if (FAsceTeamFragment* Team = EntityManager.GetFragmentDataPtr<FAsceTeamFragment>(Entity))
		{
			Team->TeamKey = TeamKeyPacked;
		}
		if (FAgentRadiusFragment* Radius = EntityManager.GetFragmentDataPtr<FAgentRadiusFragment>(Entity))
		{
			Radius->Radius = Def.Radius;
		}
		if (FAsceHeightFragment* Height = EntityManager.GetFragmentDataPtr<FAsceHeightFragment>(Entity))
		{
			Height->Height = Def.Height;
		}
		if (FAsceMovementTuningFragment* Tuning = EntityManager.GetFragmentDataPtr<FAsceMovementTuningFragment>(Entity))
		{
			Tuning->MaxSpeed = Def.MaxSpeed;
			Tuning->MaxAcceleration = Def.MaxAcceleration;
			Tuning->DefaultDesiredSpeed = Def.DefaultDesiredSpeed;
		}
		if (FAsceVisionViewFragment* View = EntityManager.GetFragmentDataPtr<FAsceVisionViewFragment>(Entity))
		{
			View->SightRadius = static_cast<uint16>(FMath::Clamp(Def.SightRadius, 1, static_cast<int32>(MAX_uint16)));
			View->EyeHeightCm = static_cast<uint16>(FMath::Clamp(Def.Height, 0.f, static_cast<float>(MAX_uint16)));
		}
	}
}

TArray<int64> UAsceEntityFactory::SpawnEntitiesByDef(UObject* WorldContextObject, FName DefRowName, const TArray<FTransform>& Transforms, int32 TeamId)
{
	TArray<int64> Result;
	if (Transforms.Num() == 0)
	{
		return Result;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return Result;
	}

	// 阵营：入参是**跨端一致的 TeamId**，写进 fragment 的 TeamKey 要打包上**本端** index（GameState 本端阵营表）
	const AAsceGameState* GameState = World->GetGameState<AAsceGameState>();
	const int32 PackedTeamKey = GameState ? GameState->GetTeamKey(TeamId) : 0;

	FAsceEntityDefRow Row;
	if (!UAsceEntityDefLibrary::GetEntityDefRowByName(DefRowName, Row))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EntityFactory] 实体定义表里没有行 '%s'"), *DefRowName.ToString());
		return Result;
	}

	UMassEntityConfigAsset* Config = Row.EntityTemplate.LoadSynchronous();
	if (!Config)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EntityFactory] 行 '%s' 没配 EntityTemplate（生成模板）"), *DefRowName.ToString());
		return Result;
	}

	UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	UMassSpawnerSubsystem* SpawnerSubsystem = World->GetSubsystem<UMassSpawnerSubsystem>();
	if (!MassSubsystem || !SpawnerSubsystem)
	{
		return Result;
	}

	const FMassEntityTemplate& EntityTemplate = Config->GetOrCreateEntityTemplate(*World);
	if (!EntityTemplate.IsValid())
	{
		return Result;
	}

	const FAsceRTSUnitDef* UnitDef = Row.Def.GetPtr<FAsceRTSUnitDef>();
	if (!UnitDef)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EntityFactory] 行 '%s' 的 Def 不是 FAsceRTSUnitDef（工厂目前只支持单位）"), *DefRowName.ToString());
	}

	TArray<FMassEntityHandle> SpawnedHandles;
	SpawnerSubsystem->SpawnEntities(EntityTemplate, Transforms.Num(), SpawnedHandles);

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	for (int32 i = 0; i < SpawnedHandles.Num(); ++i)
	{
		const FMassEntityHandle Handle = SpawnedHandles[i];
		if (!EntityManager.IsEntityValid(Handle))
		{
			continue;
		}
		if (Transforms.IsValidIndex(i))
		{
			if (FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Handle))
			{
				Transform->SetTransform(Transforms[i]);
			}
			else
			{
				// 结构契约：Transform / 身份 / 阵营是"实体基础"（单位与建筑都要）⇒ 模板必须挂 `UAsceRTSEntityTrait`
				// （`ensureOnceMsgf` 是 5.8 才有的，5.7 用本地哨兵保证只报一次，避免每个实体刷一条）
				static bool bWarnedMissingTransform = false;
				if (!bWarnedMissingTransform)
				{
					bWarnedMissingTransform = true;
					ensureMsgf(false, TEXT("[EntityFactory] 模板缺少 FTransformFragment（模板必须挂 UAsceRTSEntityTrait）"));
				}
			}
		}
		static bool bWarnedMissingIdentity = false;
		if (!bWarnedMissingIdentity
			&& EntityManager.GetFragmentDataPtr<FAsceUnitDefFragment>(Handle) == nullptr)
		{
			bWarnedMissingIdentity = true;
			ensureMsgf(false, TEXT("[EntityFactory] 模板缺少 FAsceUnitDefFragment：模板必须挂 UAsceRTSEntityTrait（身份 id / 检视器 / Ether 注册都靠它）"));
		}
		static bool bWarnedMissingTeam = false;
		if (!bWarnedMissingTeam
			&& EntityManager.GetFragmentDataPtr<FAsceTeamFragment>(Handle) == nullptr)
		{
			bWarnedMissingTeam = true;
			ensureMsgf(false, TEXT("[EntityFactory] 模板缺少 FAsceTeamFragment：模板必须挂 UAsceRTSEntityTrait"));
		}
		if (UnitDef)
		{
			ApplyUnitDef(EntityManager, Handle, *UnitDef, PackedTeamKey);
		}
		Result.Add(Handle.AsNumber());
	}
	return Result;
}
