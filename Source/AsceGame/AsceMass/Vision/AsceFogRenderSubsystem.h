// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "RHIResources.h"
#include "AsceFogRenderSubsystem.generated.h"

class FAsceFogViewExtension;

/**
 * MOD-POINT 2026-09-10 15:01:23
 * 黑雾渲染数据源（阶段 3b）：
 *  - 持有 GPU 持久 Fog 纹理对（R8，UAV 写/SRV 读，跨帧保留）：
 *      FogRawTex   = 脏 tile 展开的原始位图（1.0/0.0）
 *      FogSmoothTex= 去凸角 + 3×3 平均后的平滑结果（PS 采样它做边缘柔和）
 *  - 每帧从 UAsceFogSubsystem drain 脏 tile，组 delta payload 交给 ViewExtension；
 *  - ViewExtension 在 RDG 里展开脏 tile 写入 FogRaw，再做平滑 pass 写 FogSmooth，最后 Fullscreen 采样压黑。
 */
UCLASS(config = Game)
class UAsceFogRenderSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** 设置本地队伍键（打包键）。切换后全量标脏重建。 */
	void SetLocalTeamKey(int32 InTeamKey);

	/**
	 * 黑雾浓度：1=不可见处全黑；0.8~0.9=深灰（地形隐约可见，观感柔和）。
	 * 可在 Config/DefaultGame.ini 配置：
	 * [/Script/AsceGame.AsceFogRenderSubsystem]
	 * FogDarkness=0.85
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Asce|Fog", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FogDarkness = 0.9f;

	FIntPoint GetGridSize() const { return FIntPoint(GridW, GridH); }
	float GetCellSizeCm() const { return CellSizeCm; }
	FVector2D GetWorldOrigin() const { return WorldOrigin; }
	FTextureRHIRef GetFogTexture() const { return FogSmoothTex; }

private:
	/** 创建持久 Fog 纹理（尺寸就绪后一次）。 */
	void EnsureResources();

	/** 查询 Fog 尺寸并处理变化（尺寸变化 → 销毁纹理 + 全量标脏）。返回 Fog 是否就绪。 */
	bool UpdateFogDimensions();

	/** drain 本队脏 tile → 组 payload → 交给 ViewExtension。 */
	void FlushDirtyTiles();

	int32 GridW = 0;
	int32 GridH = 0;
	float CellSizeCm = 50.f;
	FVector2D WorldOrigin = FVector2D::ZeroVector;
	/** 本地显示队伍的队伍键（打包）。standalone 先默认 0 号队伍；联机时由本地玩家队伍接入（SetLocalTeamKey）。 */
	int32 LocalTeamKey = 0;
	bool bFogSourceDirty = false;

	FTextureRHIRef FogRawTex;     // 脏 tile 展开的原始位图（1.0/0.0）
	FTextureRHIRef FogSmoothTex;  // 去凸角 + 3×3 平均后的平滑结果（PS 采样）
	bool bFogResourcesReady = false;

	TSharedPtr<FAsceFogViewExtension> ViewExtension;
};
