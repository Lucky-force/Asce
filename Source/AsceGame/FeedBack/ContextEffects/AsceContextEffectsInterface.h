// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "Engine/HitResult.h"
#include "GameplayTagContainer.h"
#include "UObject/Interface.h"

#include "AsceContextEffectsInterface.generated.h"

#define UE_API ASCEGAME_API

class UAnimSequenceBase;
class UObject;
class USceneComponent;
struct FFrame;

/**
 *
 */
UENUM()
enum EEffectsContextMatchType: int
{
	ExactMatch,
	BestMatch
};

/**
 *
 */
UINTERFACE(MinimalAPI, Blueprintable)
class UAsceContextEffectsInterface : public UInterface
{
	GENERATED_BODY()

};
 
class IAsceContextEffectsInterface : public IInterface
{
	GENERATED_BODY()

public:

	/** 
	 * 用于在动画中生成特效，目前的用法本质上是向ContextEffect子系统请求生成Effect，这个用在移动时脚步音效和特效之类的生成
	 * @param Bone 生成Effect挂在到组件的socket
	 * @param MotionEffect 要生成的EffectTag
	 * @param StaticMeshComponent 要挂载到的组件
	 * @param LocationOffset 位置偏移
	 * @param RotationOffset 选择角度偏移
	 * @param AnimationSequence 当前播放的动画序列
	 * @param bHitSuccess 当前射线检测是否有命中
	 * @param HitResult 当前射线检测的HitResult
	 * @param Contexts 要生成的Effect的上下文Tags
	 * @param VFXScale 给niagara组件生成用
	 * @param AudioVolume 给音频组件生成用
	 * @param AudioPitch 给音频组件生成用
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	UE_API void AnimMotionEffect(const FName Bone
		, const FGameplayTag MotionEffect
		, USceneComponent* StaticMeshComponent
		, const FVector LocationOffset
		, const FRotator RotationOffset
		, const UAnimSequenceBase* AnimationSequence
		, const bool bHitSuccess
		, const FHitResult HitResult
		, FGameplayTagContainer Contexts
		, FVector VFXScale = FVector(1)
		, float AudioVolume = 1
		, float AudioPitch = 1);
};


#undef UE_API
