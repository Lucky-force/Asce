// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AsceSelectAnimVariantTask.h"

#include "MassStateTreeDependency.h"   // FStateTreeDependencyBuilder 的**定义**（只在 MassStateTreeTypes.h 前向声明不够）
#include "MassStateTreeTypes.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

#include "AsceMass/Combat/AsceCombatFragments.h"   // FAsceAnimPlaybackFragment

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceSelectAnimVariantTask)

FAsceSelectAnimVariantTask::FAsceSelectAnimVariantTask()
{
	// 只在进入状态时选一次，不需要逐帧 tick
	bShouldCallTick = false;
}

bool FAsceSelectAnimVariantTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(PlaybackHandle);
	return true;
}

void FAsceSelectAnimVariantTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadWrite(PlaybackHandle);   // 用 handle 重载 ⇒ 保持 "Optional" 语义（没动画的单位也不报错）
}

EStateTreeRunStatus FAsceSelectAnimVariantTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const
{
	if (FAsceAnimPlaybackFragment* Playback = Context.GetExternalDataPtr(PlaybackHandle))
	{
		Playback->VariantSeed = static_cast<uint8>(FMath::RandHelper(256));
	}
	return EStateTreeRunStatus::Running;
}
