// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Components/GameFrameworkInitStateInterface.h"
#include "Components/PawnComponent.h"

#include "AscePawnExtensionComponent.generated.h"

#define UE_API ASCEGAME_API

namespace EEndPlayReason { enum Type : int; }

class UGameFrameworkComponentManager;
class UAsceAbilitySystemComponent;
class UAscePawnData;
class UObject;
struct FActorInitStateChangedParams;
struct FFrame;
struct FGameplayTag;

/**
 * Component that adds functionality to all Pawn classes so it can be used for characters/vehicles/etc.
 * This coordinates the initialization of other components.
 */

//PawnComponent， 实现了IGameFrameworkInitStateInterface接口，可以维护自身的初始化状态，由此，别的组件可以通过此状态来处理依赖性

UCLASS(MinimalAPI)
class UAscePawnExtensionComponent : public UPawnComponent, public IGameFrameworkInitStateInterface
{
	GENERATED_BODY()

public:

	UE_API UAscePawnExtensionComponent(const FObjectInitializer& ObjectInitializer);

	/** The name of this overall feature, this one depends on the other named component features */
	//FeatureName, 用来根据FeatureName查询组件的初始化状态
	static UE_API const FName NAME_ActorFeatureName;

	//~ Begin IGameFrameworkInitStateInterface interface
	virtual FName GetFeatureName() const override { return NAME_ActorFeatureName; }
	
	//检测能否从当前状态转移到目标状态
	UE_API virtual bool CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const override;
	
	//这个Implementer的初始化状态改变的时候调用
	UE_API virtual void HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) override;
	
	//整个Actor的初始化状态改变时调用，ActorInitStateChangedParams中记录了FeatureName，拥有此组件的Actor，此组件对象指针，转移到的状态
	UE_API virtual void OnActorInitStateChanged(const FActorInitStateChangedParams& Params) override;
	
	//当状态转移时，会先调用HandleChangeInitState来触发Implementer状态改变时的回调， 然后立即调用Manager->ChangeFeatureInitState()
	//每一个Actor在Manager中记录了自己的ActorFeatureData，其中记录了自己的所有状态，自己的类，自己的状态转移时触发的委托列表
	//此外，Manager中还记录了某个类在状态转移时触发的委托列表，在调用ChangeFeatureInitState时，会对传入的Actor调用其自己的委托列表以及其属于的类要触发的委托列表
	//实际上，每一个ActorData都有一个FeatureData来记录Feature的状态，每个Feature只能由一个Implementer提供。ActorData里面绑定了这个Actor的任意一个Feature状态改变时要调用的委托列表
	//即广播给所有感兴趣的地方，参数中记录了FeatureName，因此接收者可以判断当前发生改变的是否是自己感兴趣的Feature
	
	//
	UE_API virtual void CheckDefaultInitialization() override;
	//~ End IGameFrameworkInitStateInterface interface

	/** Returns the pawn extension component if one exists on the specified actor. */
	UFUNCTION(BlueprintPure, Category = "Asce|Pawn")
	static UAscePawnExtensionComponent* FindPawnExtensionComponent(const AActor* Actor) { return (Actor ? Actor->FindComponentByClass<UAscePawnExtensionComponent>() : nullptr); }

	/** Gets the pawn data, which is used to specify pawn properties in data */
	template <class T>
	const T* GetPawnData() const { return Cast<T>(PawnData); }

	/** Sets the current pawn data */
	UE_API void SetPawnData(const UAscePawnData* InPawnData);

	/** Gets the current ability system component, which may be owned by a different actor */
	//获取当前的ASC，ASC可能由另一个Actor持有，比如PawnExtensionComponent在Avatar上，ASC在PlayerState上
	UFUNCTION(BlueprintPure, Category = "Asce|Pawn")
	UAsceAbilitySystemComponent* GetAsceAbilitySystemComponent() const { return AbilitySystemComponent; }

	/** Should be called by the owning pawn to become the avatar of the ability system. */
	//当此组件的拥有者Actor成为ASC的Avatar时调用
	UE_API void InitializeAbilitySystem(UAsceAbilitySystemComponent* InASC, AActor* InOwnerActor);

	/** Should be called by the owning pawn to remove itself as the avatar of the ability system. */
	//当此组件的拥有者Actor不再是ASC的Avatar时调用
	UE_API void UninitializeAbilitySystem();

	/** Should be called by the owning pawn when the pawn's controller changes. */
	//当Controller改变时调用
	UE_API void HandleControllerChanged();

	/** Should be called by the owning pawn when the player state has been replicated. */
	//PlayerState复制时调用
	UE_API void HandlePlayerStateReplicated();

	/** Should be called by the owning pawn when the input component is setup. */
	//输入组件设置的时候调用
	UE_API void SetupPlayerInputComponent();

	/** Register with the OnAbilitySystemInitialized delegate and broadcast if our pawn has been registered with the ability system component */
	//注册OnAbilitySystemInitialized委托，并且如果这个组件的拥有者Pawn注册成功，则调用一次
	UE_API void OnAbilitySystemInitialized_RegisterAndCall(FSimpleMulticastDelegate::FDelegate Delegate);

	/** Register with the OnAbilitySystemUninitialized delegate fired when our pawn is removed as the ability system's avatar actor */
	//此组件的拥有者Pawn不再是ASC的Avatar时移除委托回调函数
	UE_API void OnAbilitySystemUninitialized_Register(FSimpleMulticastDelegate::FDelegate Delegate);

protected:

	UE_API virtual void OnRegister() override;
	UE_API virtual void BeginPlay() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	UE_API void OnRep_PawnData();

	/** Delegate fired when our pawn becomes the ability system's avatar actor */
	//此组件的拥有者Pawn成为ASC的Avatar时触发的委托
	FSimpleMulticastDelegate OnAbilitySystemInitialized;

	/** Delegate fired when our pawn is removed as the ability system's avatar actor */
	//此组件的拥有者Pawn不再是ASC的Avatar时触发的委托
	FSimpleMulticastDelegate OnAbilitySystemUninitialized;

	/** Pawn data used to create the pawn. Specified from a spawn function or on a placed instance. */
	//用来创建Pawn的PanwData
	UPROPERTY(EditInstanceOnly, ReplicatedUsing = OnRep_PawnData, Category = "Asce|Pawn")
	TObjectPtr<const UAscePawnData> PawnData;

	/** Pointer to the ability system component that is cached for convenience. */
	UPROPERTY(Transient)
	TObjectPtr<UAsceAbilitySystemComponent> AbilitySystemComponent;
};

#undef UE_API
