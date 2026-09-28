// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"
#include "Components/GameFrameworkInitStateInterface.h"
#include "Components/PawnComponent.h"
#include "UObject/Object.h"
#include "AsceObserverComponent.generated.h"
#define UE_API ASCEGAME_API
struct FInputMappingContextAndPriority;
struct FInputActionValue;
class UAsceInputConfig;
class UAsceCameraMode;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDragSelect, const FVector2D&, const FVector2D&)
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSelect, const FVector2D&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMoveOrder, const FVector2D&);

/**
 * 给观察者使用的组件，提供鼠标拖拽屏幕移动视角, 框选单位等rts操作的功能
 */
UCLASS(MinimalAPI, Blueprintable, Meta=(BlueprintSpawnableComponent))
class UAsceObserverComponent : public UPawnComponent, public IGameFrameworkInitStateInterface
{
	GENERATED_BODY()
	
public:

	UE_API UAsceObserverComponent(const FObjectInitializer& ObjectInitializer);
	
	UFUNCTION(BlueprintPure, Category = "Asce|Observer")
	static UAsceObserverComponent* FindHeroComponent(const AActor* Actor) { return (Actor ? Actor->FindComponentByClass<UAsceObserverComponent>() : nullptr); }

	UFUNCTION(BlueprintCallable, Category = "Asce|Observer")
	void SetActorLocation(FVector InLocation);
	
	UFUNCTION(BlueprintCallable, Category = "Asce|Observer")
	FVector GetActorLocation();
	
	/** Overrides the camera from an active gameplay ability */
	UE_API void SetAbilityCameraMode(TSubclassOf<UAsceCameraMode> CameraMode, const FGameplayAbilitySpecHandle& OwningSpecHandle);

	/** Clears the camera override if it is set */
	UE_API void ClearAbilityCameraMode(const FGameplayAbilitySpecHandle& OwningSpecHandle);

	/** 绑定用来释放技能的inputconfig */
	UE_API void AddAdditionalInputConfig(const UAsceInputConfig* InputConfig);
	
	UE_API void RemoveAdditionalInputConfig(const UAsceInputConfig* InputConfig);
	
	UE_API bool IsReadyToBindInputs() const;
	
	/** The name of the extension event sent via UGameFrameworkComponentManager when ability inputs are ready to bind */
	static UE_API const FName NAME_BindInputsNow;

	/** The name of this component-implemented feature */
	static UE_API const FName NAME_ActorFeatureName;

	//~ Begin IGameFrameworkInitStateInterface interface
	virtual FName GetFeatureName() const override { return NAME_ActorFeatureName; }
	UE_API virtual bool CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const override;
	UE_API virtual void HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) override;
	UE_API virtual void OnActorInitStateChanged(const FActorInitStateChangedParams& Params) override;
	UE_API virtual void CheckDefaultInitialization() override;
	//~ End IGameFrameworkInitStateInterface interface

protected:

	UE_API virtual void OnRegister() override;
	UE_API virtual void BeginPlay() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UE_API virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	
	UE_API virtual void InitializePlayerInput(UInputComponent* PlayerInputComponent);

	UE_API TSubclassOf<UAsceCameraMode> DetermineCameraMode() const;
	
	//输入处理
	UE_API void Input_AbilityInputTagPressed(FGameplayTag InputTag);
	UE_API void Input_AbilityInputTagReleased(FGameplayTag InputTag);
	
	UE_API void Input_MovePerspective(const FInputActionValue& InputActionValue);
	UE_API void Input_Shifting_Start(const FInputActionValue& InputActionValue);
	UE_API void Input_Shifting_Stop(const FInputActionValue& InputActionValue);
	UE_API void Input_Alternate_Start(const FInputActionValue& InputActionValue);
	UE_API void Input_Alternate_Stop(const FInputActionValue& InputActionValue);
	UE_API void Input_Control_Start(const FInputActionValue& InputActionValue);
	UE_API void Input_Control_Stop(const FInputActionValue& InputActionValue);
	UE_API void Input_Select_Start(const FInputActionValue& InputActionValue);
	UE_API void Input_Select_Stop(const FInputActionValue& InputActionValue);
	UE_API void Input_Select_Cancel(const FInputActionValue& InputActionValue);
	UE_API void Input_Order_Move(const FInputActionValue& InputActionValue);
	
public:
	//在对应事件触发的时候，通过委托进行广播
	FOnDragSelect OnDragSelect;
	FOnDragSelect OnAdditiveDragSelect;
	FOnSelect OnAdditiveSelect;
	FOnSelect OnSelect;
	FOnSelect OnAlternateSelect;
	FOnSelect OnControlSelect;
	//右键下发移动指令（带屏幕坐标，世界坐标由消费方反投影——见AsceRTSCommanderComponent）
	FOnMoveOrder OnMoveOrder;
	
protected:
	//是否处于按键shifting状态(按着shift)
	UPROPERTY()
	bool bIsShifting = false;
	
	//是否处于按键Alternating状态(按着alt)
	UPROPERTY()
	bool bIsAlternating = false;
	
	//是否处于按键Controlling状态(按着ctrl)
	UPROPERTY()
	bool bIsControlling = false;
	
	//是否处于选择状态（按着左键）
	UPROPERTY()
	bool bIsSelecting = false;
	
	//在Select开始的时候缓存一次这些值，之后怎么变化的都不管了
	UPROPERTY()
	bool bCachedIsShifting = false;
	
	UPROPERTY()
	bool bCachedIsAlternating = false;
	
	UPROPERTY()
	bool bCachedIsControlling = false;
	
	//选中状态的起点(这两个数据仅在Selecting结束的时候使用)
	UPROPERTY()
	FVector2D SelectionStartPosition = FVector2D::ZeroVector;
	
	//选中状态的终点
	UPROPERTY()
	FVector2D SelectionEndPosition = FVector2D::ZeroVector;
	
	//框选触发的最小像素阈值
	UPROPERTY()
	float DragSelectPixelThreshold = 3;
	
public:
	/*这些Get函数并不推荐使用，通常来说只需要绑定上面的委托来获取操作，这里仅仅是为了方便widget每帧绘制图形而暴露出来的*/
	UFUNCTION(BlueprintCallable)
	bool IsShifting() const { return bIsShifting; }
	
	UFUNCTION(BlueprintCallable)
	bool IsAlternating() const { return bIsAlternating; }
	
	UFUNCTION(BlueprintCallable)
	bool IsControlling() const { return bIsControlling; }
	
	UFUNCTION(BlueprintCallable)
	bool IsSelecting() const { return bIsSelecting; }
	
	UFUNCTION(BlueprintCallable)
	FVector2D GetSelectionStartPosition() const { return SelectionStartPosition; }
	
	UFUNCTION(BlueprintCallable)
	FVector2D GetSelectionEndPosition() const { return SelectionEndPosition; }
	
protected:
	
	UPROPERTY(EditAnywhere)
	TArray<FInputMappingContextAndPriority> DefaultInputMappings;
	
	/** Camera mode set by an ability. */
	UPROPERTY()
	TSubclassOf<UAsceCameraMode> AbilityCameraMode;

	/** 设置这个CameraMode的AbilitySpecHandle（暂时预留） */
	FGameplayAbilitySpecHandle AbilityCameraModeOwningSpecHandle;

	/** True when player input bindings have been applied, will never be true for non - players */
	bool bReadyToBindInputs;
	
	//缓存这一帧的actor位置更新，等到最后统一处理
	UPROPERTY()
	FVector CachedLocationToUpdate = FVector::ZeroVector;

	//视角焦点上报的节流状态（仅本端客户端使用）
	float TimeSinceLastViewFocusUpdate = 0.f;
	FVector LastSentViewFocus = FVector::ZeroVector;
	bool bHasSentViewFocus = false;

public:
	UPROPERTY(EditAnywhere)
	float MoveSpeed = 100.f;

	/** 视角焦点上报间隔（秒）：服务器端viewer（= 服务器那份Pawn的位置）按此频率跟随本端相机焦点 */
	UPROPERTY(EditAnywhere, Category = "Asce|Observer")
	float ViewFocusUpdateInterval = 0.1f;

	/** 焦点相对上次上报移动超过这个距离（cm）才上报；静止不动时不上报 */
	UPROPERTY(EditAnywhere, Category = "Asce|Observer")
	float ViewFocusUpdateThreshold = 10.f;
};

#undef UE_API
