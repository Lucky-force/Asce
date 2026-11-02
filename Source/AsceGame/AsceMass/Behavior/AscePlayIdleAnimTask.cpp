// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AscePlayIdleAnimTask.h"

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

#include UE_INLINE_GENERATED_CPP_BY_NAME(AscePlayIdleAnimTask)

namespace
{
	/** idlebreak 间隔的随机区间（秒）——写死，改这里即可 */
	constexpr float MinIdleBreakInterval = 4.f;
	constexpr float MaxIdleBreakInterval = 8.f;
}

FAscePlayIdleAnimTask::FAscePlayIdleAnimTask()
{
	bShouldCallTick = true;
}

bool FAscePlayIdleAnimTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(PlaybackHandle);
	Linker.LinkExternalData(UnitDefHandle);
	return true;
}

void FAscePlayIdleAnimTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadWrite(PlaybackHandle);
	Builder.AddReadOnly(UnitDefHandle);
}

float FAscePlayIdleAnimTask::RollNextBreakInterval()
{
	return FMath::FRandRange(MinIdleBreakInterval, MaxIdleBreakInterval);
}

float FAscePlayIdleAnimTask::PlayRandomBreak(FAsceAnimPlaybackFragment& Playback, const FAsceRTSUnitDef& UnitDef, const float PlayRateScale)
{
	const UAsceAnimSet* AnimSet = UnitDef.AnimSet.LoadSynchronous();
	const TSoftObjectPtr<UAnimSequence>* Sequence = AsceAnimCue::PickRandom(UnitDef.IdleBreakAnims);
	if (!Sequence || !AsceAnimCue::PlaySequence(Playback, AnimSet, *Sequence, /*bLoop*/ false, PlayRateScale))
	{
		return 0.f;
	}

	const FAsceAnimClip* Clip = AnimSet ? AnimSet->Clips.Find(*Sequence) : nullptr;
	if (!Clip)
	{
		return 0.f;
	}
	const float ClipSeconds = static_cast<float>(Clip->EndFrame - Clip->StartFrame)
		/ FMath::Max(AnimSet->FrameRate, 1.f)
		/ FMath::Max(Playback.CuePlayRate, 0.01f);
	return FMath::Max(ClipSeconds, 0.05f);
}

EStateTreeRunStatus FAscePlayIdleAnimTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	Data.bPlayingBreak = false;
	Data.BreakRemaining = 0.f;
	Data.TimeUntilBreak = RollNextBreakInterval();

	UWorld* World = Context.GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return EStateTreeRunStatus::Running;   // 专用服务器：只占状态
	}

	FAsceAnimPlaybackFragment* Playback = Context.GetExternalDataPtr(PlaybackHandle);
	if (!Playback)
	{
		return EStateTreeRunStatus::Running;
	}

	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	FAsceRTSUnitDef UnitDef;
	if (!AsceAnimCue::GetUnitDef(MassContext, UnitDef))
	{
		return EStateTreeRunStatus::Running;
	}

	const UAsceAnimSet* AnimSet = UnitDef.AnimSet.LoadSynchronous();
	if (!UnitDef.IdleAnim.IsNull())
	{
		AsceAnimCue::PlaySequence(*Playback, AnimSet, UnitDef.IdleAnim, /*bLoop*/ true, Data.PlayRateScale);
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FAscePlayIdleAnimTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

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

	// 正在播 idlebreak：走完就回 idle，期间**不累计**间隔
	if (Data.bPlayingBreak)
	{
		Data.BreakRemaining -= DeltaTime;
		if (Data.BreakRemaining <= 0.f)
		{
			Data.bPlayingBreak = false;
			Data.TimeUntilBreak = RollNextBreakInterval();

			const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
			FAsceRTSUnitDef UnitDef;
			if (AsceAnimCue::GetUnitDef(MassContext, UnitDef))
			{
				const UAsceAnimSet* AnimSet = UnitDef.AnimSet.LoadSynchronous();
				if (!UnitDef.IdleAnim.IsNull())
				{
					AsceAnimCue::PlaySequence(*Playback, AnimSet, UnitDef.IdleAnim, /*bLoop*/ true, Data.PlayRateScale);
				}
			}
		}
		return EStateTreeRunStatus::Running;
	}

	// 计时到 → 摇一个 idlebreak
	Data.TimeUntilBreak -= DeltaTime;
	if (Data.TimeUntilBreak <= 0.f)
	{
		const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
		FAsceRTSUnitDef UnitDef;
		if (AsceAnimCue::GetUnitDef(MassContext, UnitDef))
		{
			const float BreakSeconds = PlayRandomBreak(*Playback, UnitDef, Data.PlayRateScale);
			if (BreakSeconds > 0.f)
			{
				Data.bPlayingBreak = true;
				Data.BreakRemaining = BreakSeconds;
				return EStateTreeRunStatus::Running;
			}
		}
		Data.TimeUntilBreak = RollNextBreakInterval();   // 没配 break：跳过这一轮
	}

	return EStateTreeRunStatus::Running;
}
