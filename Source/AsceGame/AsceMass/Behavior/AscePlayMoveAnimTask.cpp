// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AscePlayMoveAnimTask.h"

#include "Engine/World.h"
#include "MassMovementFragments.h"
#include "MassStateTreeDependency.h"
#include "MassStateTreeExecutionContext.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

#include "AsceMass/Behavior/AsceAnimCueLibrary.h"
#include "AsceMass/Combat/AsceAnimDef.h"
#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Data/AsceUnitDef.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AscePlayMoveAnimTask)

namespace
{
	/** 奔跑阈值：当前速度 ≥ 基础速度 × 1.7 */
	constexpr float RunSpeedMultiplier = 1.7f;

	/** 受伤移动阈值：生命值 < 15% */
	constexpr float InjuredHealthRatio = 0.15f;

	enum class EBucket : int32
	{
		Injured = 0,
		Run = 1,
		Walk = 2,
	};
}

FAscePlayMoveAnimTask::FAscePlayMoveAnimTask()
{
	bShouldCallTick = true;
}

bool FAscePlayMoveAnimTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(PlaybackHandle);
	Linker.LinkExternalData(UnitDefHandle);
	return true;
}

void FAscePlayMoveAnimTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadWrite(PlaybackHandle);
	Builder.AddReadOnly(UnitDefHandle);
}

EStateTreeRunStatus FAscePlayMoveAnimTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.CurrentBucket = INDEX_NONE;   // 进入时强制选一次动画
	return UpdateAnimation(Context, Data);
}

EStateTreeRunStatus FAscePlayMoveAnimTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	return UpdateAnimation(Context, Data);
}

EStateTreeRunStatus FAscePlayMoveAnimTask::UpdateAnimation(FStateTreeExecutionContext& Context, FInstanceDataType& Data) const
{
	UWorld* World = Context.GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return EStateTreeRunStatus::Running;
	}

	FAsceAnimPlaybackFragment* Playback = Context.GetExternalDataPtr(PlaybackHandle);
	if (!Playback)
	{
		return EStateTreeRunStatus::Running;
	}

	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	const FMassEntityManager& EntityManager = MassContext.GetEntityManager();
	const FMassEntityHandle Entity = MassContext.GetEntity();

	// 桶：受伤 > 奔跑 > 普通
	const FAsceHealthFragment* Health = EntityManager.GetFragmentDataPtr<FAsceHealthFragment>(Entity);
	const bool bInjured = Health && Health->MaxHealth > 0.f
		&& (Health->CurrentHealth / Health->MaxHealth) < InjuredHealthRatio;

	float Speed2D = 0.f;
	if (const FMassVelocityFragment* Velocity = EntityManager.GetFragmentDataPtr<FMassVelocityFragment>(Entity))
	{
		Speed2D = static_cast<float>(Velocity->Value.Size2D());
	}

	FAsceRTSUnitDef UnitDef;
	if (!AsceAnimCue::GetUnitDef(MassContext, UnitDef))
	{
		return EStateTreeRunStatus::Running;
	}

	const float RunThreshold = UnitDef.BaseMoveSpeed * RunSpeedMultiplier;
	const EBucket Bucket = bInjured
		? EBucket::Injured
		: (Speed2D >= RunThreshold ? EBucket::Run : EBucket::Walk);

	if (static_cast<int32>(Bucket) == Data.CurrentBucket)
	{
		return EStateTreeRunStatus::Running;
	}
	Data.CurrentBucket = static_cast<int32>(Bucket);

	const TArray<TSoftObjectPtr<UAnimSequence>>* Animations = &UnitDef.MoveAnims;
	if (Bucket == EBucket::Injured)
	{
		Animations = UnitDef.InjuredMoveAnims.Num() > 0 ? &UnitDef.InjuredMoveAnims : &UnitDef.MoveAnims;
	}
	else if (Bucket == EBucket::Run)
	{
		Animations = UnitDef.RunAnims.Num() > 0 ? &UnitDef.RunAnims : &UnitDef.MoveAnims;
	}

	if (const TSoftObjectPtr<UAnimSequence>* Sequence = AsceAnimCue::PickRandom(*Animations))
	{
		AsceAnimCue::PlaySequence(*Playback, UnitDef.AnimSet.LoadSynchronous(), *Sequence, /*bLoop*/ true, Data.PlayRateScale);
	}

	return EStateTreeRunStatus::Running;
}
