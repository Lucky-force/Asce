#pragma once

#include "Engine/World.h"
#include "GameplayTagContainer.h"

#include "AsceCameraMode.generated.h"

#define UE_API ASCEGAME_API

class AActor;
class UCanvas;
class UAsceCameraComponent;

/**
 * EAsceCameraModeBlendFunction
 *
 *	Blend function used for transitioning between camera modes.
 *	相机模式之间混合的方式
 */
UENUM(BlueprintType)
enum class EAsceCameraModeBlendFunction : uint8
{
	// Does a simple linear interpolation.
	// 线性插值
	Linear,

	// Immediately accelerates, but smoothly decelerates into the target.  Ease amount controlled by the exponent.
	// 凸函数曲线, 由Exponent控制系数
	EaseIn,

	// Smoothly accelerates, but does not decelerate into the target.  Ease amount controlled by the exponent.
	// 凹函数曲线，由Exponent控制系数
	EaseOut,

	// Smoothly accelerates and decelerates.  Ease amount controlled by the exponent.
	// 斜率先增加再减少，Expoinent控制系数
	EaseInOut,

	COUNT	UMETA(Hidden)
};


/**
 * FAsceCameraModeView
 *
 *	View data produced by the camera mode that is used to blend camera modes.
 *	每个相机模式都代表了一个拍摄的距离，角度，fov
 *	相机混合的时候从一个相机模式混合到另一个相机模式，CameraModeView记录了相机模式的这些数据
 */
struct FAsceCameraModeView
{
public:

	FAsceCameraModeView();

	//从当前的View混合到目标View, 这个View直接在CameraManager中用来计算最终的视口画面渲染
	//当前相机就是目前位于相机栈顶层的相机，
	void Blend(const FAsceCameraModeView& Other, float OtherWeight);

public:

	FVector Location;
	FRotator Rotation;
	FRotator ControlRotation;
	float FieldOfView;
};


/**
 * UAsceCameraMode
 *
 *	Base class for all camera modes.
 *	相机模式的基类
 */
UCLASS(MinimalAPI, Abstract, NotBlueprintable)
class UAsceCameraMode : public UObject
{
	GENERATED_BODY()

public:

	UE_API UAsceCameraMode();

	UE_API UAsceCameraComponent* GetAsceCameraComponent() const;

	UE_API virtual UWorld* GetWorld() const override;

	//要拍摄的对象
	UE_API AActor* GetTargetActor() const;

	const FAsceCameraModeView& GetCameraModeView() const { return View; }

	// Called when this camera mode is activated on the camera mode stack.
	//相机模式激活时调用
	virtual void OnActivation() {};

	// Called when this camera mode is deactivated on the camera mode stack.
	//相机模式取消激活时调用
	virtual void OnDeactivation() {};

	UE_API void UpdateCameraMode(float DeltaTime);

	float GetBlendTime() const { return BlendTime; }
	float GetBlendWeight() const { return BlendWeight; }
	UE_API void SetBlendWeight(float Weight);

	FGameplayTag GetCameraTypeTag() const
	{
		return CameraTypeTag;
	}

	UE_API virtual void DrawDebug(UCanvas* Canvas) const;

protected:

	//获取枢轴位置， 相机一定是围绕一个点来进行拍摄的（类比极坐标系中的原点），这个点就是枢轴（pivot），也可以叫支点
	UE_API virtual FVector GetPivotLocation() const;
	UE_API virtual FRotator GetPivotRotation() const;

	UE_API virtual void UpdateView(float DeltaTime);
	UE_API virtual void UpdateBlending(float DeltaTime);

protected:
	// A tag that can be queried by gameplay code that cares when a kind of camera mode is active
	// without having to ask about a specific mode (e.g., when aiming downsights to get more accuracy)
	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	FGameplayTag CameraTypeTag;

	// View output produced by the camera mode.
	FAsceCameraModeView View;

	// The horizontal field of view (in degrees).
	UPROPERTY(EditDefaultsOnly, Category = "View", Meta = (UIMin = "5.0", UIMax = "170", ClampMin = "5.0", ClampMax = "170.0"))
	float FieldOfView;

	// Minimum view pitch (in degrees).
	UPROPERTY(EditDefaultsOnly, Category = "View", Meta = (UIMin = "-89.9", UIMax = "89.9", ClampMin = "-89.9", ClampMax = "89.9"))
	float ViewPitchMin;

	// Maximum view pitch (in degrees).
	UPROPERTY(EditDefaultsOnly, Category = "View", Meta = (UIMin = "-89.9", UIMax = "89.9", ClampMin = "-89.9", ClampMax = "89.9"))
	float ViewPitchMax;

	// How long it takes to blend in this mode.
	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	float BlendTime;

	// Function used for blending.
	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	EAsceCameraModeBlendFunction BlendFunction;

	// Exponent used by blend functions to control the shape of the curve.
	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	float BlendExponent;

	// Linear blend alpha used to determine the blend weight.
	float BlendAlpha;

	// Blend weight calculated using the blend alpha and function.
	float BlendWeight;

protected:
	/** If true, skips all interpolation and puts camera in ideal location.  Automatically set to false next frame. */
	UPROPERTY(transient)
	uint32 bResetInterpolation:1;
};


/**
 * UAsceCameraModeStack
 *
 *	Stack used for blending camera modes.
 *	存放相机模式的栈
 */
UCLASS()
class UAsceCameraModeStack : public UObject
{
	GENERATED_BODY()

public:

	UAsceCameraModeStack();

	void ActivateStack();
	void DeactivateStack();

	bool IsStackActivate() const { return bIsActive; }

	//将某个类的相机模式压栈（从Instances池子中取出实例）
	void PushCameraMode(TSubclassOf<UAsceCameraMode> CameraModeClass);

	bool EvaluateStack(float DeltaTime, FAsceCameraModeView& OutCameraModeView);

	void DrawDebug(UCanvas* Canvas) const;

	// Gets the tag associated with the top layer and the blend weight of it
	void GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const;

protected:

	UAsceCameraMode* GetCameraModeInstance(TSubclassOf<UAsceCameraMode> CameraModeClass);

	void UpdateStack(float DeltaTime);
	void BlendStack(FAsceCameraModeView& OutCameraModeView) const;

protected:

	bool bIsActive;

	//理想情况下，只会有一个相机使用相机模式，即玩家相机。（对于演出，临时的电影式相机，直接setviewtraget到对应的相机上即可）
	//所有的CameraMode的实例都放在这个实例池中，随后在其中取出来放到相机栈中。这个实例池类似一个对象池
	UPROPERTY()
	TArray<TObjectPtr<UAsceCameraMode>> CameraModeInstances;

	UPROPERTY()
	TArray<TObjectPtr<UAsceCameraMode>> CameraModeStack;
};

#undef UE_API
