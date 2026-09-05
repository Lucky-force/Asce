// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "MassEntityTypes.h"
#include "MassCommonTypes.h"
#include "AsceInteractionFragments.generated.h"

/**
 * 交互系统骨架：具体交互行为（拾取/采集/对话等）各自独立Processor监听
 * UMassSignalSubsystem广播的"进入交互范围"信号去处理，不侵入这里的通用流程。
 */

UENUM(BlueprintType)
enum class EAsceInteractionState : uint8
{
	None,
	MovingToRange,
	Interacting,
};

// 挂在"可被交互"的Entity上（比如资源点、可拾取物、NPC）
USTRUCT(BlueprintType)
struct FAsceInteractableFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	float InteractionRange = 150.f;
};

// 挂在"发起交互"的Entity上（比如玩家单位），记录当前的交互意图
USTRUCT(BlueprintType)
struct FAsceInteractionOrderFragment : public FMassFragment
{
	GENERATED_BODY()

	// 交互目标用NetID寻址，理由同FAsceAttackOrderFragment
	UPROPERTY()
	FMassNetworkID TargetNetID;

	UPROPERTY(BlueprintReadWrite)
	EAsceInteractionState State = EAsceInteractionState::None;
};

// 任何单位都能触发的通用交互（比如拾取物品）
USTRUCT()
struct FAsceGenericInteractableTag : public FMassTag
{
	GENERATED_BODY()
};

// 只有玩家控制的单位才能触发的交互（比如任务NPC对话）
USTRUCT()
struct FAscePlayerOnlyInteractableTag : public FMassTag
{
	GENERATED_BODY()
};
