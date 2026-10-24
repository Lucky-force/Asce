// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AsceTimelineLibrary.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

#include "AsceMass/Behavior/AsceBehaviorFragment.h"

namespace AsceTimeline
{
	double GetServerNow(const UWorld& World)
	{
		if (const AGameStateBase* GameState = World.GetGameState())
		{
			return GameState->GetServerWorldTimeSeconds();
		}
		return World.GetTimeSeconds();
	}

	float GetTime(const UWorld& World, const FAsceBehaviorFragment& Behavior)
	{
		if (!Behavior.IsTimelineActive())
		{
			return 0.f;
		}
		// 用 double 相减再转 float：长时间运行也不丢精度（float 只有 7 位有效数字）
		return static_cast<float>(GetServerNow(World) - Behavior.TimelineStartWorldTime);
	}

	bool HasReached(const UWorld& World, const FAsceBehaviorFragment& Behavior, const float TargetTime)
	{
		return Behavior.IsTimelineActive() && GetTime(World, Behavior) >= TargetTime;
	}
}
