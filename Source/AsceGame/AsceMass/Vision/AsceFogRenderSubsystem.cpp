// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceFogRenderSubsystem.h"

#include "AsceFogViewExtension.h"
#include "AsceFogSubsystem.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "PixelFormat.h"
#include "RenderingThread.h"
#include "SceneViewExtension.h"

// MOD-POINT 2026-09-10 15:01:23

void UAsceFogRenderSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAsceFogRenderSubsystem::Deinitialize()
{
	ViewExtension.Reset();
	bTexturesReady = false;
	Super::Deinitialize();
}

bool UAsceFogRenderSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// Dedicated Server 无渲染：不创建 RHI 纹理/ViewExtension（null RHI 上会无效甚至崩溃）
	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}
	return World->IsGameWorld() || World->IsPlayInEditor();
}

void UAsceFogRenderSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
}

TStatId UAsceFogRenderSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAsceFogRenderSubsystem, STATGROUP_Tickables);
}

void UAsceFogRenderSubsystem::SetLocalTeamKey(int32 InTeamKey)
{
	if (LocalTeamKey != InTeamKey)
	{
		LocalTeamKey = InTeamKey;
		// 新队伍没有历史 fog：全量标脏，下一 tick 重建整图
		if (UAsceFogSubsystem* Fog = UAsceFogSubsystem::Get(this))
		{
			if (Fog->IsReady())
			{
				Fog->MarkAllTilesDirty(LocalTeamKey);
			}
		}
	}
}

bool UAsceFogRenderSubsystem::UpdateFogDimensions()
{
	UAsceFogSubsystem* Fog = UAsceFogSubsystem::Get(this);
	if (!Fog || !Fog->IsReady())
	{
		return false;
	}

	const FAsceVisionHeightMap& HM = Fog->GetHeightMap();
	const int32 NewW = HM.GetGridW();
	const int32 NewH = HM.GetGridH();
	const float NewCell = HM.GetCellSizeCm();

	if (NewW != GridW || NewH != GridH || !FMath::IsNearlyEqual(NewCell, CellSizeCm))
	{
		GridW = NewW;
		GridH = NewH;
		CellSizeCm = NewCell;
		bTexturesReady = false; // 尺寸变了：下一 tick 重发创建命令（新纹理覆盖，旧引用自然释放）
		// 尺寸变化 = 全图重建：把所有 tile 标脏（首个 tick 也能覆盖全图）
		Fog->MarkAllTilesDirty(LocalTeamKey);
	}
	return true;
}

void UAsceFogRenderSubsystem::FlushDirtyTiles()
{
	if (!ViewExtension.IsValid())
	{
		return; // 没有消费者就别 drain，避免丢标记
	}

	UAsceFogSubsystem* Fog = UAsceFogSubsystem::Get(this);
	if (!Fog || !Fog->IsReady())
	{
		return;
	}

	TArray<int32> TileIndices;
	Fog->DrainDirtyTiles(LocalTeamKey, TileIndices);
	if (TileIndices.Num() == 0)
	{
		return;
	}

	TArray<uint32> TileWords;
	TileWords.Reserve(TileIndices.Num() * UAsceFogSubsystem::TileWords);

	TArray<uint32> OneTile;
	for (const int32 TileIndex : TileIndices)
	{
		Fog->ExtractTileWords(LocalTeamKey, TileIndex, OneTile);
		TileWords.Append(OneTile);
	}

	ViewExtension->EnqueueDelta(TileIndices, TileWords,
		UAsceFogSubsystem::TileWords, Fog->GetTilesX(), UAsceFogSubsystem::TileCells);
}

void UAsceFogRenderSubsystem::Tick(float DeltaTime)
{
	// 自 tick 已停用：本 tick 体已挪成 `RunFogRenderStage`，由 `UAsceSimulationSubsystem` 按阶段顺序调用
	Super::Tick(DeltaTime);
}

void UAsceFogRenderSubsystem::RunFogRenderStage(const FAsceTickContext& Ctx)
{
	const bool bFogReady = UpdateFogDimensions();
	if (!bFogReady)
	{
		return;
	}

	// 创建后处理 ViewExtension（绑定本世界；不等纹理，纹理就绪后同一帧的命令会设置它）
	if (!ViewExtension.IsValid())
	{
		ViewExtension = FSceneViewExtensions::NewExtension<FAsceFogViewExtension>(GetWorld());
	}
	if (!ViewExtension.IsValid())
	{
		return;
	}

	// 纹理创建/重建：FRHICommandListImmediate 仅渲染线程可用，GT Tick 不能直接 CreateTexture，
	// 发渲染命令在 RT 创建 + SetSource_RenderThread。
	// 时序：GT 先清 pending（尺寸变化时旧 tile 索引作废）→ 发创建命令（RT FIFO）→ 本帧 FlushDirtyTiles
	// 入队的 delta 在渲染 pass 时仍在 pending 中（创建命令不清 pending）→ 不会丢首帧/重建帧数据。
	if (!bTexturesReady)
	{
		ViewExtension->ClearPendingDelta();
		bTexturesReady = true;

		const int32 W = GridW;
		const int32 H = GridH;
		const float Cell = CellSizeCm;
		const FVector2D Origin = WorldOrigin;
		const float Dark = FogDarkness;
		TSharedPtr<FAsceFogViewExtension> Ext = ViewExtension; // 拷贝保活（渲染命令异步执行）
		ENQUEUE_RENDER_COMMAND(AsceFogCreateTextures)(
			[Ext, W, H, Cell, Origin, Dark](FRHICommandListImmediate& RHICmdList)
			{
				// 持久 Fog 纹理对：R8（PF_R8 = R8_UNORM；UNORM 支持 UAV 写入 + 双线性采样），
				// UAV 写 / SRV 读，跨帧保留内容。
				// FogRaw=脏tile展开原始位图；FogSmooth=去凸角+3×3平均后的平滑结果（PS 采样）。
				const uint32 TexW = static_cast<uint32>(FMath::Max(1, W));
				const uint32 TexH = static_cast<uint32>(FMath::Max(1, H));

				FRHITextureCreateDesc RawDesc = FRHITextureCreateDesc::Create2D(TEXT("AsceFogRawTex"), TexW, TexH, PF_R8);
				RawDesc.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
				FTextureRHIRef Raw = RHICmdList.CreateTexture(RawDesc);

				FRHITextureCreateDesc SmoothDesc = FRHITextureCreateDesc::Create2D(TEXT("AsceFogSmoothTex"), TexW, TexH, PF_R8);
				SmoothDesc.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
				FTextureRHIRef Smooth = RHICmdList.CreateTexture(SmoothDesc);

				if (!Raw.IsValid() || !Smooth.IsValid())
				{
					UE_LOG(LogTemp, Error, TEXT("[AsceFog] Fog 资源创建失败"));
				}

				if (Ext.IsValid())
				{
					Ext->SetSource_RenderThread(MoveTemp(Raw), MoveTemp(Smooth), FIntPoint(W, H), Cell, Origin, Dark);
				}
			});
	}

	// drain 脏 tile（ViewExtension 已就绪；创建命令在 RT FIFO 前段执行，会先设置纹理/参数）
	FlushDirtyTiles();
}
