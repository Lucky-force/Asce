// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AsceAnimCueLibrary.h"

#include "MassStateTreeExecutionContext.h"

#include "AsceMass/Combat/AsceAnimDef.h"
#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "AsceMass/Data/AsceUnitDef.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"

bool AsceAnimCue::GetUnitDef(const FMassStateTreeExecutionContext& MassContext, FAsceRTSUnitDef& OutDef)
{
	const FMassEntityManager& EntityManager = MassContext.GetEntityManager();
	const FMassEntityHandle Entity = MassContext.GetEntity();
	const FAsceUnitDefFragment* UnitDefFragment = EntityManager.GetFragmentDataPtr<FAsceUnitDefFragment>(Entity);
	if (!UnitDefFragment)
	{
		return false;
	}

	FAsceEntityDefRow Row;
	if (!UAsceEntityDefLibrary::GetEntityDefRow(UnitDefFragment->UniqueTypeId, Row))
	{
		return false;
	}
	const FAsceRTSUnitDef* UnitDef = Row.Def.GetPtr<FAsceRTSUnitDef>();
	if (!UnitDef)
	{
		return false;
	}
	OutDef = *UnitDef;
	return true;
}

const TSoftObjectPtr<UAnimSequence>* AsceAnimCue::PickRandom(const TArray<TSoftObjectPtr<UAnimSequence>>& Animations)
{
	TArray<const TSoftObjectPtr<UAnimSequence>*> Valid;
	Valid.Reserve(Animations.Num());
	for (const TSoftObjectPtr<UAnimSequence>& Animation : Animations)
	{
		if (!Animation.IsNull())
		{
			Valid.Add(&Animation);
		}
	}
	if (Valid.Num() == 0)
	{
		return nullptr;
	}
	return Valid[FMath::RandHelper(Valid.Num())];
}

bool AsceAnimCue::PlaySequence(FAsceAnimPlaybackFragment& Playback, const UAsceAnimSet* AnimSet
	, const TSoftObjectPtr<UAnimSequence>& Sequence, const bool bLoop, const float PlayRate)
{
	if (!AnimSet || Sequence.IsNull())
	{
		return false;
	}
	const FAsceAnimClip* Clip = AnimSet->Clips.Find(Sequence);
	if (!Clip)
	{
		return false;
	}

	Playback.bCueActive = true;
	Playback.CueAnimIndex = Clip->AnimIndex;
	Playback.CueStartFrame = Clip->StartFrame;
	Playback.CueEndFrame = Clip->EndFrame;
	Playback.CueFrameRate = FMath::Max(AnimSet->FrameRate, 1.f);
	Playback.CuePlayRate = FMath::Max(PlayRate, 0.01f);
	Playback.bCueLoop = bLoop;
	++Playback.CueCounter;   // 变化 → visual processor 重拾
	return true;
}

bool AsceAnimCue::IsPlayingSequence(const FAsceAnimPlaybackFragment& Playback, const UAsceAnimSet* AnimSet
	, const TSoftObjectPtr<UAnimSequence>& Sequence)
{
	if (!Playback.bCueActive || !AnimSet || Sequence.IsNull())
	{
		return false;
	}
	const FAsceAnimClip* Clip = AnimSet->Clips.Find(Sequence);
	if (!Clip)
	{
		return false;
	}
	return Playback.CueAnimIndex == Clip->AnimIndex
		&& Playback.CueStartFrame == Clip->StartFrame
		&& Playback.CueEndFrame == Clip->EndFrame;
}
