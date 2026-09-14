// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceFogViewExtension.h"

#include "AsceFogShaders.h"
#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHIStaticStates.h"
#include "RenderingThread.h"
#include "Engine/World.h"
#include "Algo/BinarySearch.h"

// MOD-POINT 2026-09-10 15:01:23
// MOD-POINT 2026-09-10 16:20:00  阶段 2b：RDG Compute(BitMap→Fog) + Fullscreen(压黑 SceneColor)

FAsceFogViewExtension::FAsceFogViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld)
	: FSceneViewExtensionBase(AutoRegister)
	, ExtensionWorld(InWorld)
{
}

void FAsceFogViewExtension::SetSource_RenderThread(FTextureRHIRef InFogRaw, FTextureRHIRef InFogSmooth, FIntPoint InGridSize, float InCellSizeCm,
	FVector2D InWorldOrigin, float InDarkness)
{
	bEnabled.store(InFogRaw.IsValid() && InFogSmooth.IsValid() && InGridSize.X > 0 && InGridSize.Y > 0, std::memory_order_relaxed);

	// 仅渲染线程访问的参数：直接赋值（本函数只允许在渲染线程调用）
	FogRawTex = MoveTemp(InFogRaw);
	FogSmoothTex = MoveTemp(InFogSmooth);
	GridSize = InGridSize;
	CellSizeCm = InCellSizeCm;
	WorldOrigin = InWorldOrigin;
	Darkness = InDarkness;
	CachedFogRawRT = nullptr;    // 纹理变了：池化包装重建
	CachedFogSmoothRT = nullptr;
}

void FAsceFogViewExtension::ClearPendingDelta()
{
	FScopeLock Lock(&DeltaLock);
	PendingTileIndices.Reset();
	PendingTileWords.Reset();
}

bool FAsceFogViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	if (!bEnabled.load(std::memory_order_relaxed))
	{
		return false;
	}

	// 只作用于本扩展绑定世界（创建它的 GameWorld）的 view family：
	// 防 PIE 编辑器视口、其它 PIE 世界/多客户端 view family 之间互相串雾数据。
	const UWorld* ContextWorld = Context.GetWorld();
	return ContextWorld && ExtensionWorld.IsValid() && ContextWorld == ExtensionWorld.Get();
}

void FAsceFogViewExtension::EnqueueDelta(const TArray<int32>& InTileIndices, const TArray<uint32>& InTileWordsData,
	int32 InTileWordsPerTile, int32 InTilesX, int32 InTileCells)
{
	FScopeLock Lock(&DeltaLock);
	TileWordsPerTile = FMath::Max(1, InTileWordsPerTile);
	TilesX = InTilesX;
	TileCells = FMath::Max(1, InTileCells);

	const int32 WordsPerTile = TileWordsPerTile;
	for (int32 i = 0; i < InTileIndices.Num(); ++i)
	{
		const int32 TileIndex = InTileIndices[i];
		const uint32* Src = InTileWordsData.GetData() + i * WordsPerTile;

		const int32 Pos = Algo::LowerBound(PendingTileIndices, TileIndex);
		if (Pos < PendingTileIndices.Num() && PendingTileIndices[Pos] == TileIndex)
		{
			// 已存在：覆盖为最新（保证同一 tile 一次 dispatch 只出现一次）
			FMemory::Memcpy(PendingTileWords.GetData() + Pos * WordsPerTile, Src, WordsPerTile * sizeof(uint32));
		}
		else
		{
			// 二分位置插入，保持升序
			PendingTileIndices.Insert(TileIndex, Pos);
			const int32 WordInsert = Pos * WordsPerTile;
			PendingTileWords.InsertUninitialized(WordInsert, WordsPerTile);
			FMemory::Memcpy(PendingTileWords.GetData() + WordInsert, Src, WordsPerTile * sizeof(uint32));
		}
	}
}

void FAsceFogViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View,
	FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	// 阶段 2a：只在 Tonemap 阶段挂一个直通回调，验证 hook。
	// 阶段 2b：在该回调里用 RDG 加 Compute + Fullscreen，替换直通。
	if (bEnabled && PassId == EPostProcessingPass::Tonemap)
	{
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(
			this, &FAsceFogViewExtension::PostProcessPassAfterTonemap_RenderThread));
	}
}

FScreenPassTexture FAsceFogViewExtension::PostProcessPassAfterTonemap_RenderThread(FRDGBuilder& GraphBuilder,
	const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	FScreenPassTexture SceneColor(Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!FogRawTex.IsValid() || !FogSmoothTex.IsValid() || GridSize.X <= 0 || GridSize.Y <= 0)
	{
		return SceneColor;
	}

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());

	// 持久 Fog（外部纹理）→ RDG：池化包装缓存一次（仅纹理变更时由SetSource置空重建），
	// 不再每帧 CreateRenderTarget。
	if (!CachedFogRawRT.IsValid() && FogRawTex.IsValid())
	{
		CachedFogRawRT = CreateRenderTarget(FogRawTex.GetReference(), TEXT("AsceFogRawTex"));
	}
	if (!CachedFogSmoothRT.IsValid() && FogSmoothTex.IsValid())
	{
		CachedFogSmoothRT = CreateRenderTarget(FogSmoothTex.GetReference(), TEXT("AsceFogSmoothTex"));
	}
	if (!CachedFogRawRT.IsValid() || !CachedFogSmoothRT.IsValid())
	{
		return SceneColor;
	}
	FRDGTextureRef RDGFogRaw = GraphBuilder.RegisterExternalTexture(CachedFogRawRT, TEXT("AsceFogRawTex"));
	FRDGTextureRef RDGFogSmooth = GraphBuilder.RegisterExternalTexture(CachedFogSmoothRT, TEXT("AsceFogSmoothTex"));

	// 消费本帧入队的脏 tile（GT 写、此处 render 线程取走）
	TArray<int32> TileIndices;
	TArray<uint32> TileWordsData;
	int32 InTileWords = 128;
	int32 InTilesX = 0;
	int32 InTileCells = 64;
	{
		FScopeLock Lock(&DeltaLock);
		TileIndices = MoveTemp(PendingTileIndices);
		TileWordsData = MoveTemp(PendingTileWords);
		InTileWords = TileWordsPerTile;
		InTilesX = TilesX;
		InTileCells = TileCells;
	}

	// 1) Compute：只展开脏 tile 写入持久 FogRaw（干净 tile 保留旧值）
	if (TileIndices.Num() > 0)
	{
		TArray<uint32> Payload;
		Payload.Reserve(TileIndices.Num() + TileWordsData.Num());
		for (const int32 TileIndex : TileIndices)
		{
			Payload.Add(static_cast<uint32>(TileIndex));
		}
		Payload.Append(TileWordsData);

		FRDGBufferRef PayloadBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("AsceFogDelta"), Payload);

		TShaderRef<FAsceFogCS> ComputeShader = ShaderMap->GetShader<FAsceFogCS>();
		FAsceFogCS::FParameters* CSParams = GraphBuilder.AllocParameters<FAsceFogCS::FParameters>();
		CSParams->Payload = GraphBuilder.CreateSRV(PayloadBuffer);
		CSParams->OutFog = GraphBuilder.CreateUAV(RDGFogRaw);
		CSParams->GridW = GridSize.X;
		CSParams->GridH = GridSize.Y;
		CSParams->TilesCount = TileIndices.Num();
		CSParams->TilesX = InTilesX;
		CSParams->TileCells = InTileCells;
		CSParams->TileWords = InTileWords;

		FComputeShaderUtils::AddPass(
			GraphBuilder, RDG_EVENT_NAME("AsceFogDeltaCS"), ComputeShader, CSParams,
			FIntVector(TileIndices.Num(), 1, 1));

		// 2) 边缘平滑（有脏 tile 展开后才需要重算；FogSmooth 是持久纹理，干净帧保留旧结果）：
		//    Pass1 去凸角（读 FogRaw 写 transient Corner）→ Pass2 3×3 平均（读 Corner 写 FogSmooth）。
		const FIntVector SmoothGroups(
			FMath::DivideAndRoundUp(GridSize.X, 8),
			FMath::DivideAndRoundUp(GridSize.Y, 8),
			1);

		FRDGTextureDesc CornerDesc = FRDGTextureDesc::Create2D(
			GridSize, PF_R8, FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef RDGCorner = GraphBuilder.CreateTexture(CornerDesc, TEXT("AsceFogCorner"));

		{
			TShaderRef<FAsceFogCornerCS> CornerShader = ShaderMap->GetShader<FAsceFogCornerCS>();
			FAsceFogCornerCS::FParameters* CornerParams = GraphBuilder.AllocParameters<FAsceFogCornerCS::FParameters>();
			CornerParams->FogRawTexture = RDGFogRaw;
			CornerParams->CornerUAV = GraphBuilder.CreateUAV(RDGCorner);
			CornerParams->GridSize = GridSize;
			FComputeShaderUtils::AddPass(
				GraphBuilder, RDG_EVENT_NAME("AsceFogCornerCS"), CornerShader, CornerParams, SmoothGroups);
		}

		{
			TShaderRef<FAsceFogSmoothCS> SmoothShader = ShaderMap->GetShader<FAsceFogSmoothCS>();
			FAsceFogSmoothCS::FParameters* SmoothParams = GraphBuilder.AllocParameters<FAsceFogSmoothCS::FParameters>();
			SmoothParams->FogCornerTexture = RDGCorner;
			SmoothParams->FogSmoothUAV = GraphBuilder.CreateUAV(RDGFogSmooth);
			SmoothParams->GridSize = GridSize;
			FComputeShaderUtils::AddPass(
				GraphBuilder, RDG_EVENT_NAME("AsceFogSmoothCS"), SmoothShader, SmoothParams, SmoothGroups);
		}
	}

	// 输出 RT（优先用后处理框架指定的 OverrideOutput）
	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(
			GraphBuilder, SceneColor, View.GetOverwriteLoadAction(), TEXT("AsceFog"));
	}

	FRDGTextureRef DepthTexture = Inputs.SceneTextures.SceneTextures
		? Inputs.SceneTextures.SceneTextures->GetContents()->SceneDepthTexture
		: nullptr;
	if (!DepthTexture)
	{
		return Output;
	}

	// 2) Fullscreen PS：采样持久 Fog 压黑 SceneColor
	{
		TShaderRef<FAsceFogPS> PixelShader = ShaderMap->GetShader<FAsceFogPS>();
		FAsceFogPS::FParameters* PSParams = GraphBuilder.AllocParameters<FAsceFogPS::FParameters>();
		PSParams->View = View.ViewUniformBuffer;
		PSParams->SceneColorTexture = SceneColor.Texture;
		PSParams->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PSParams->SceneDepthTexture = DepthTexture;
		PSParams->FogTexture = RDGFogSmooth;
		PSParams->FogSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PSParams->GridSize = GridSize;
		PSParams->WorldOrigin = FVector2f((float)WorldOrigin.X, (float)WorldOrigin.Y);
		PSParams->CellSize = CellSizeCm;
		PSParams->Darkness = Darkness;
		PSParams->RenderTargets[0] = Output.GetRenderTargetBinding();

		AddDrawScreenPass(
			GraphBuilder, RDG_EVENT_NAME("AsceFogPS"),
			FScreenPassViewInfo(View),
			FScreenPassTextureViewport(Output),
			FScreenPassTextureViewport(SceneColor),
			PixelShader, PSParams);
	}

	return Output;
}
