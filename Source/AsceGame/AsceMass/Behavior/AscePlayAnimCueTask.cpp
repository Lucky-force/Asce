// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AscePlayAnimCueTask.h"

#include "Engine/World.h"
#include "MassStateTreeDependency.h"
#include "MassStateTreeTypes.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Combat/AsceAnimDef.h"
#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "AsceMass/Data/AsceUnitDef.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"   // FAsceUnitDefFragment（AddReadOnly 的概念约束需要完整类型）

#include UE_INLINE_GENERATED_CPP_BY_NAME(AscePlayAnimCueTask)

FAscePlayAnimCueTask::FAscePlayAnimCueTask()
{
	// 只在进入状态时写一次 cue
	bShouldCallTick = false;
}

bool FAscePlayAnimCueTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(PlaybackHandle);
	Linker.LinkExternalData(UnitDefHandle);
	return true;
}

void FAscePlayAnimCueTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadWrite(PlaybackHandle);   // handle 重载 ⇒ 保持 Optional 语义
	Builder.AddReadOnly(UnitDefHandle);
}

EStateTreeRunStatus FAscePlayAnimCueTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	UWorld* World = Context.GetWorld();
	if (!World)
	{
		return Data.bHoldState ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
	}
	// 只在会渲染的端写 cue（专用服务器不写）
	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return Data.bHoldState ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
	}

	FAsceAnimPlaybackFragment* Playback = Context.GetExternalDataPtr(PlaybackHandle);
	if (!Playback)
	{
		return Data.bHoldState ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
	}

	// 从候选里随机选一条（两端各自随机；表现层不一致可接受）
	if (Data.Variants.Num() == 0)
	{
		Playback->bCueActive = false;
		Playback->bValid = false;   // 不要留着上一条 cue（比如走路）继续播
		return Data.bHoldState ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
	}
	const TSoftObjectPtr<UAnimSequence> Clip = Data.Variants[FMath::RandHelper(Data.Variants.Num())];
	if (Clip.IsNull())
	{
		Playback->bCueActive = false;
		Playback->bValid = false;
		return Data.bHoldState ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
	}

	// 解析到 AnimSet 的绝对帧段（fragment 只存 POD，不放资产引用）
	const FAsceUnitDefFragment* UnitDef = Context.GetExternalDataPtr(UnitDefHandle);
	UAsceAnimSet* AnimSet = nullptr;
	if (UnitDef)
	{
		FAsceEntityDefRow DefRow;
		if (UAsceEntityDefLibrary::GetEntityDefRow(UnitDef->UniqueTypeId, DefRow))
		{
			if (const FAsceRTSUnitDef* RTSDef = DefRow.Def.GetPtr<FAsceRTSUnitDef>())
			{
				AnimSet = RTSDef->AnimSet.LoadSynchronous();
			}
		}
	}
	const FAsceAnimClip* AnimClip = AnimSet ? AnimSet->Clips.Find(Clip) : nullptr;
	if (!AnimClip)
	{
		Playback->bCueActive = false;
		Playback->bValid = false;   // 不要留着上一条 cue 继续播
		UE_LOG(LogTemp, Warning, TEXT("[Anim] cue 动画 %s 不在 AnimSet(%s).Clips 里（单位类型 %u）"),
			*Clip.ToString(), AnimSet ? *AnimSet->GetName() : TEXT("无"), UnitDef ? UnitDef->UniqueTypeId : 0);
		return Data.bHoldState ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
	}

	Playback->bCueActive = true;
	Playback->CueAnimIndex = AnimClip->AnimIndex;
	Playback->CueStartFrame = AnimClip->StartFrame;
	Playback->CueEndFrame = AnimClip->EndFrame;
	Playback->CueFrameRate = AnimSet ? FMath::Max(AnimSet->FrameRate, 1.f) : 30.f;
	Playback->CuePlayRate = FMath::Max(Data.PlayRate, 0.01f);
	Playback->bCueLoop = Data.bLoop;
	++Playback->CueCounter;   // 变了 ⇒ 表现 processor 重播
	// 片长（秒）：用烘焙帧段/帧率算，和 PlayRate 一起决定实际播放时长
	const float ClipSeconds = (static_cast<float>(AnimClip->EndFrame - AnimClip->StartFrame) / Playback->CueFrameRate)
		/ Playback->CuePlayRate;
	UE_LOG(LogTemp, Verbose, TEXT("[Anim] cue %s（单位 %u, Hold=%d, Loop=%d, Dur=%.2fs）"),
		*Clip.ToString(), UnitDef ? UnitDef->UniqueTypeId : 0,
		Data.bHoldState ? 1 : 0, Data.bLoop ? 1 : 0, ClipSeconds);
	return Data.bHoldState ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
}
