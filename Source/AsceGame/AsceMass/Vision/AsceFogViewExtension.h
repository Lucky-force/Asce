// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "SceneViewExtension.h"
#include "SceneViewExtensionContext.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "HAL/CriticalSection.h"
#include "RenderTargetPool.h"
#include <atomic>

class UWorld;

/**
 * 黑雾后处理 ViewExtension：
 *  - RDG 里 Compute(脏 tile 位图 → 持久 Fog) + Fullscreen(采样 Fog 压黑 SceneColor)；
 *  - 世界绑定：只有创建它的 UWorld（GameWorld）的 view family 才生效——防止 PIE 编辑器视口、
 *    其它 PIE 世界/多客户端之间互相串雾数据（IsActiveThisFrame_Internal 里比对 Context.GetWorld()）；
 *  - 线程模型：FogRawTex/FogSmoothTex/GridSize/CellSize/WorldOrigin/Darkness/Cached*RT 仅渲染线程读写
 *    （SetSource 在 GT 调用，通过 ENQUEUE_RENDER_COMMAND 提交参数，消除 GT/RT 数据竞争）；
 *    bEnabled 原子（GT 写、GT/RT 读）；脏 tile 数组走锁（GT 写、RT drain）。
 */
class FAsceFogViewExtension : public FSceneViewExtensionBase
{
public:
	explicit FAsceFogViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld);

	/** 设置持久 Fog 纹理与显示参数（GridSize/CellSize/WorldOrigin/Darkness）。GT 调用。
	 *  InFogRaw=脏tile展开的原始位图；InFogSmooth=去凸角+3×3平均后的平滑结果（PS 采样它）。 */
	void SetSource(FTextureRHIRef InFogRaw, FTextureRHIRef InFogSmooth, FIntPoint InGridSize, float InCellSizeCm,
		FVector2D InWorldOrigin, float InDarkness);

	/** 入队一批脏 tile（GT 调用；render 线程在构建 graph 时取走消费）。
	 *  按 tile 去重合并：已存在的 tile 用新 words 覆盖（保证总是最新、且上限 = tile 总数）。 */
	void EnqueueDelta(const TArray<int32>& InTileIndices, const TArray<uint32>& InTileWordsData,
		int32 InTileWordsPerTile, int32 InTilesX, int32 InTileCells);

	// ISceneViewExtension
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View,
		FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

private:
	FScreenPassTexture PostProcessPassAfterTonemap_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	/** 创建它的世界（GameWorld）；只有该世界的 view family 才应用本扩展。 */
	TWeakObjectPtr<UWorld> ExtensionWorld;

	/** 启用标志：GT 写（SetSource），GT/RT 读（IsActiveThisFrame / pass），原子。 */
	std::atomic<bool> bEnabled{ false };

	// ── 以下参数仅渲染线程访问（SetSource 经 ENQUEUE_RENDER_COMMAND 提交） ──
	FTextureRHIRef FogRawTex;     // 脏 tile 展开的原始位图（1.0/0.0）
	FTextureRHIRef FogSmoothTex;  // 去凸角 + 3×3 平均后的平滑结果（PS 采样目标）
	/** FogRawTex / FogSmoothTex 的池化包装，缓存复用避免每帧新建。 */
	TRefCountPtr<IPooledRenderTarget> CachedFogRawRT;
	TRefCountPtr<IPooledRenderTarget> CachedFogSmoothRT;
	FIntPoint GridSize = FIntPoint::ZeroValue;
	FVector2D WorldOrigin = FVector2D::ZeroVector;
	float CellSizeCm = 50.f;
	float Darkness = 1.f;

	// ── 待消费的脏 tile（GT 写、render 线程读，锁保护） ──
	FCriticalSection DeltaLock;
	TArray<int32> PendingTileIndices;   // 升序（二分去重）
	TArray<uint32> PendingTileWords;    // 与 Indices 平行：第 i 项占 TileWordsPerTile 个 word
	int32 TileWordsPerTile = 128;
	int32 TilesX = 0;
	int32 TileCells = 64;
};
