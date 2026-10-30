// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"

#include "AsceEntityModularEditor/AsceUnitModule.h"

class FPreviewScene;
class UAsceAnimSet;
struct FAsceAnimClip;
struct FAsceAnimClip;
class UInstancedStaticMeshComponent;

/** 预览视口客户端：把模块参数画成几何（视野圈 / 攻击圈 / 体积胶囊 + 1m 网格） */
class FAsceUnitModulePreviewClient : public FEditorViewportClient
{
public:
	FAsceUnitModulePreviewClient(FPreviewScene* InPreviewScene, const TSharedPtr<SEditorViewport>& InViewportWidget);

	/** 用"模块汇总出来的预览产物"刷新几何（圆/胶囊/盒） */
	void ApplyPreview(const FAsceUnitModulePreviewActions& Actions);

	/** 设置要预览的动画（VAT：ISM + 每实例 custom data 的帧号）；AnimSet/ClipIndex 无效时清空 */
	void SetAnim(UAsceAnimSet* InAnimSet, const FAsceAnimClip& Clip);

	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** 几何预览（由各模块 CollectPreview 汇总） */
	TArray<FAsceUnitModulePreviewActions::FCirclePreview> Circles;
	TArray<FAsceUnitModulePreviewActions::FCapsulePreview> Capsules;
	TArray<FAsceUnitModulePreviewActions::FBoxPreview> Boxes;

	/** 动画预览（VAT）：ISM 一实例，custom data [0]=当前帧、[1]=上一帧 */
	void EnsureMeshComponent(const UAsceAnimSet* InAnimSet);
	void ApplyFrame(bool bForce);

	TStrongObjectPtr<UAsceAnimSet> AnimSet;
	TWeakObjectPtr<UInstancedStaticMeshComponent> MeshComponent;
	int32 ClipFirstFrame = 0;
	int32 ClipLastFrame = 0;
	float ClipFrameRate = 30.f;
	float PlayRate = 1.f;
	float FrameAccumulator = 0.f;
	int32 CurrentFrame = INDEX_NONE;
	int32 PreviousFrame = INDEX_NONE;
};

/**
 * 编辑器预览视口（`SEditorViewport` + `FPreviewScene`）。
 * 只画几何，不 spawn 实体、不跑 Mass —— L1 预览。
 */
class ASCEEDITOR_API SAsceUnitModulePreviewViewport : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SAsceUnitModulePreviewViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** 刷新预览参数 */
	void ApplyPreview(const FAsceUnitModulePreviewActions& Actions);

	/** 设置要预览的动画（VAT：ISM 一实例 + per-instance custom data 帧号） */
	void SetAnim(UAsceAnimSet* InAnimSet, const FAsceAnimClip& Clip);

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	TSharedPtr<FPreviewScene> PreviewScene;
	TSharedPtr<FAsceUnitModulePreviewClient> PreviewClient;
};
