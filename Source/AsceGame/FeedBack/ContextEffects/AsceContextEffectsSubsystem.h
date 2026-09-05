// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"

#include "AsceContextEffectsSubsystem.generated.h"

#define UE_API ASCEGAME_API

enum EPhysicalSurface : int;

class AActor;
class UAudioComponent;
class UAsceContextEffectsLibrary;
class UNiagaraComponent;
class USceneComponent;
struct FFrame;
struct FGameplayTag;
struct FGameplayTagContainer;

/**
 *
 */
UCLASS(MinimalAPI, config = Game, defaultconfig, meta = (DisplayName = "LyraContextEffects"))
class UAsceContextEffectsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	//物理表面到EffectTag的映射，此处映射到GameplayTag，再用这个GameplayTag去查对应的Effects
	UPROPERTY(config, EditAnywhere)
	TMap<TEnumAsByte<EPhysicalSurface>, FGameplayTag> SurfaceTypeToContextMap;
};

/**
 * EffectSet,存放了多个ContextEffectsLibrary
 */
UCLASS(MinimalAPI)
class UAsceContextEffectsSet : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TSet<TObjectPtr<UAsceContextEffectsLibrary>> AsceContextEffectsLibraries;
};


/**
 * 
 */
UCLASS(MinimalAPI)
class UAsceContextEffectsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
	
public:
	/** 生成ContextEffects 
	 * @param SpawningActor 这个Effect要生成在哪个Actor身上
	 * @param AttachToComponent 这个Effect要Attach到这个Actor的哪个组件上
	 * @param AttachPoint 这个Effect要Attach到哪个socket上
	 * @param LocationOffset Attach位置偏移
	 * @param RotationOffset Attach旋转偏移
	 * @param Effect 要添加的Effect的Tag
	 * @param Contexts 上下文tags，可能有多个具有相同tag的effect，但是其上下文tag不同，根据上下文做出额外的筛选。比如有两个effect的tag都是武器普通攻击，但是一个的上下文tag是刀，另一个是枪
	 * @param AudioOut 本次生成Effects时生成的音频组件指针返回到这个数组中
	 * @param NiagaraOut 本次生成Effects时生成的Niagara组件指针返回到这个数组中
	 * @param VFXScale 特效缩放，传给Niagara生成
	 * @param AudioVolume 音量，传给音频组件生成
	 * @param AudioPitch 声音频率，传给音频组件生成
	 */
	UFUNCTION(BlueprintCallable, Category = "ContextEffects")
	UE_API void SpawnContextEffects(
		const AActor* SpawningActor
		, USceneComponent* AttachToComponent
		, const FName AttachPoint
		, const FVector LocationOffset
		, const FRotator RotationOffset
		, FGameplayTag Effect
		, FGameplayTagContainer Contexts
		, TArray<UAudioComponent*>& AudioOut
		, TArray<UNiagaraComponent*>& NiagaraOut
		, FVector VFXScale = FVector(1)
		, float AudioVolume = 1
		, float AudioPitch = 1);

	/** 从物理表面类型获取GameplayTag */
	UFUNCTION(BlueprintCallable, Category = "ContextEffects")
	UE_API bool GetContextFromSurfaceType(TEnumAsByte<EPhysicalSurface> PhysicalSurface, FGameplayTag& Context);

	/** 为某个Actor重新加载新的EffectLibraries */
	UFUNCTION(BlueprintCallable, Category = "ContextEffects")
	UE_API void LoadAndAddContextEffectsLibraries(AActor* OwningActor, TSet<TSoftObjectPtr<UAsceContextEffectsLibrary>> ContextEffectsLibraries);

	/** */
	UFUNCTION(BlueprintCallable, Category = "ContextEffects")
	UE_API void UnloadAndRemoveContextEffectsLibraries(AActor* OwningActor);

private:

	//存放每个Actor所挂载的Effect集合，每个集合上面有多个EffectLibrary，
	UPROPERTY(Transient)
	TMap<TObjectPtr<AActor>, TObjectPtr<UAsceContextEffectsSet>> ActiveActorEffectsMap;

};

#undef UE_API
