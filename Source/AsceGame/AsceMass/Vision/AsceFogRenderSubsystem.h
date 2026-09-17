// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AsceMass/Simulation/AsceTickContext.h"

#include "Subsystems/WorldSubsystem.h"
#include "RHIResources.h"
#include "AsceFogRenderSubsystem.generated.h"

class FAsceFogViewExtension;

/**
 * MOD-POINT 2026-09-10 15:01:23
 * 黑雾渲染数据源（阶段 3b）：
 *  - 每帧从 UAsceFogSubsystem drain 脏 tile，组 delta payload 交给 ViewExtension；
 *  - 纹理创建（R8 持久纹理对：FogRaw 原始位图 + FogSmooth 平滑结果）通过
 *    "AsceFogCreateTextures" 渲染命令在渲染线程完成（FRHICommandListImmediate 仅渲染线程可用，
 *    GT Tick 里不能直接 CreateTexture）；ViewExtension 在 RDG 里展开脏 tile 写 FogRaw，
 *    再做平滑 pass 写 FogSmooth，最后 Fullscreen 采样压黑。
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

	/** **阶段入口**（由 `UAsceSimulationSubsystem` 调用；本系统已关闭自 tick）：
	 *  把本帧雾结果/dirty tile 上传到渲染侧。 */
	void RunFogRenderStage(const FAsceTickContext& Ctx);

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

private:
	/** 查询 Fog 尺寸并处理变化（尺寸变化 → 全量标脏 + 请求重建纹理）。返回 Fog 是否就绪。 */
	bool UpdateFogDimensions();

	/** drain 本队脏 tile → 组 payload → 交给 ViewExtension。 */
	void FlushDirtyTiles();

	int32 GridW = 0;
	int32 GridH = 0;
	float CellSizeCm = 50.f;
	FVector2D WorldOrigin = FVector2D::ZeroVector;
	/** 本地显示队伍的队伍键（打包）。standalone 先默认 0 号队伍；联机时由本地玩家队伍接入（SetLocalTeamKey）。 */
	int32 LocalTeamKey = 0;
	/**
	 * 纹理是否已按当前尺寸请求创建（GT 标志）。尺寸变化时置 false，
	 * Tick 里发 "AsceFogCreateTextures" 渲染命令（GT 不能直接 CreateTexture，
	 * FRHICommandListImmediate 仅渲染线程可用）后置 true。
	 */
	bool bTexturesReady = false;

	TSharedPtr<FAsceFogViewExtension> ViewExtension;
};
