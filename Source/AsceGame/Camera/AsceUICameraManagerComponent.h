// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Camera/PlayerCameraManager.h"

#include "AsceUICameraManagerComponent.generated.h"

class AAscePlayerCameraManager;

class AActor;
class AHUD;
class APlayerController;
class FDebugDisplayInfo;
class UCanvas;
class UObject;

//专门管理 UI 层的摄像机模式，与角色的 Gameplay Camera 分开。
//在进入不同 UI 状态时（例如打开角色界面、进入前端菜单），自动切换到对应的 CameraMode。
//提供接口让 UI 系统调用，而不用直接操作 Gameplay Camera。
//保证 UI 摄像机和游戏摄像机之间的过渡平滑，不会出现突兀的切换。


UCLASS( Transient, Within=AscePlayerCameraManager )
class UAsceUICameraManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	static UAsceUICameraManagerComponent* GetComponent(APlayerController* PC);

public:
	UAsceUICameraManagerComponent();	
	virtual void InitializeComponent() override;

	bool IsSettingViewTarget() const { return bUpdatingViewTarget; }
	AActor* GetViewTarget() const { return ViewTarget; }
	void SetViewTarget(AActor* InViewTarget, FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams());

	bool NeedsToUpdateViewTarget() const;
	void UpdateViewTarget(struct FTViewTarget& OutVT, float DeltaTime);

	void OnShowDebugInfo(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos);

private:
	UPROPERTY(Transient)
	TObjectPtr<AActor> ViewTarget;
	
	UPROPERTY(Transient)
	bool bUpdatingViewTarget;
};
