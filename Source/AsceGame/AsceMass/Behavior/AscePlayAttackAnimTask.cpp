// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AscePlayAttackAnimTask.h"

#include "Engine/World.h"
#include "MassStateTreeDependency.h"
#include "MassStateTreeExecutionContext.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

#include "AsceMass/Behavior/AsceAnimCueLibrary.h"
#include "AsceMass/Combat/AsceAnimDef.h"
#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Data/AsceUnitDef.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AscePlayAttackAnimTask)

FAscePlayAttackAnimTask::FAscePlayAttackAnimTask()
{
	bShouldCallTick = false;
}

bool FAscePlayAttackAnimTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(PlaybackHandle);
	Linker.LinkExternalData(UnitDefHandle);
	return true;
}

void FAscePlayAttackAnimTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadWrite(PlaybackHandle);
	Builder.AddReadOnly(UnitDefHandle);
}

EStateTreeRunStatus FAscePlayAttackAnimTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	UWorld* World = Context.GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	FAsceAnimPlaybackFragment* Playback = Context.GetExternalDataPtr(PlaybackHandle);
	if (!Playback)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	FAsceRTSUnitDef UnitDef;
	if (!AsceAnimCue::GetUnitDef(MassContext, UnitDef))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	const UAsceAnimSet* AnimSet = UnitDef.AnimSet.LoadSynchronous();
	const TSoftObjectPtr<UAnimSequence>* Sequence = AsceAnimCue::PickRandom(UnitDef.AttackAnims);
	if (!Sequence)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	float PlayRate = 1.f;
	if (const FAsceCombatAttributesFragment* Combat = MassContext.GetEntityManager().GetFragmentDataPtr<FAsceCombatAttributesFragment>(MassContext.GetEntity()))
	{
		const float BaseAttackSpeed = UnitDef.BaseAttackSpeed > 0.f ? UnitDef.BaseAttackSpeed : 180.f;
		PlayRate = FMath::Clamp(Combat->AttackSpeed / BaseAttackSpeed, 0.05f, 4.f);
	}

	AsceAnimCue::PlaySequence(*Playback, AnimSet, *Sequence, /*bLoop*/ false, PlayRate * Data.PlayRateScale);
	return EStateTreeRunStatus::Succeeded;
}
