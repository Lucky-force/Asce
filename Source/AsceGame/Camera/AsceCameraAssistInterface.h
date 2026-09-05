// Fill out your copyright notice in the Description page of Project Settings.

#pragma once


#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "AsceCameraAssistInterface.generated.h"

/** */
UINTERFACE(BlueprintType)
class UAsceCameraAssistInterface : public UInterface
{
	GENERATED_BODY()
};

class IAsceCameraAssistInterface
{
	GENERATED_BODY()

public:
	/**
	 * Get the list of actors that we're allowing the camera to penetrate. Useful in 3rd person cameras
	 * when you need the following camera to ignore things like the a collection of view targets, the pawn,
	 * a vehicle..etc.
	 * 获取我们运行相机穿透的Actors， 在第三人称相机中很有用，当你需要跟随的相机忽略一些东西时，比如角色在开车，希望相机忽略车的碰撞
	 */
	virtual void GetIgnoredActorsForCameraPentration(TArray<const AActor*>& OutActorsAllowPenetration) const { }

	/**
	 * The target actor to prevent penetration on.  Normally, this is almost always the view target, which if
	 * unimplemented will remain true.  However, sometimes the view target, isn't the same as the root actor 
	 * you need to keep in frame.
	 * 获取阻止相机穿透的目标角色，通常这总是这个相机的viewtarget
	 */
	virtual TOptional<AActor*> GetCameraPreventPenetrationTarget() const
	{
		return TOptional<AActor*>();
	}

	/** Called if the camera penetrates the focal target.  Useful if you want to hide the target actor when being overlapped. */
	// 在相机穿透了这个拍摄角色的时候调用，此时可以把角色设置为透明或者隐藏来处理表现效果
	virtual void OnCameraPenetratingTarget() { }
};
