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
	FogRawTex = nullptr;
	FogSmoothTex = nullptr;
	bFogResourcesReady = false;
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
		FogRawTex = nullptr;
		FogSmoothTex = nullptr;
		bFogResourcesReady = false;
		bFogSourceDirty = true;
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

void UAsceFogRenderSubsystem::EnsureResources()
{
	if (bFogResourcesReady || GridW <= 0 || GridH <= 0)
	{
		return;
	}

	// 持久 Fog 纹理对：R8（PF_R8 = R8_UNORM；UNORM 支持 UAV 写入 + 双线性采样），
	// GridW×GridH，UAV 写 / SRV 读，跨帧保留内容。
	// FogRaw=脏tile展开原始位图；FogSmooth=去凸角+3×3平均后的平滑结果（PS 采样）。
	FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
	FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(
		TEXT("AsceFogRawTex"), static_cast<uint32>(GridW), static_cast<uint32>(GridH), PF_R8);
	Desc.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
	FogRawTex = RHICmdList.CreateTexture(Desc);

	Desc = FRHITextureCreateDesc::Create2D(
		TEXT("AsceFogSmoothTex"), static_cast<uint32>(GridW), static_cast<uint32>(GridH), PF_R8);
	Desc.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
	FogSmoothTex = RHICmdList.CreateTexture(Desc);

	bFogResourcesReady = FogRawTex.IsValid() && FogSmoothTex.IsValid();
	if (!bFogResourcesReady)
	{
		UE_LOG(LogTemp, Error, TEXT("[AsceFog] Fog 资源创建失败"));
	}
}

void UAsceFogRenderSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const bool bFogReady = UpdateFogDimensions();

	if (!bFogResourcesReady)
	{
		EnsureResources();
	}

	// 资源就绪后创建后处理 ViewExtension（绑定本世界，只有本 GameWorld 的 view family 生效）
	if (bFogReady && bFogResourcesReady && !ViewExtension.IsValid())
	{
		ViewExtension = FSceneViewExtensions::NewExtension<FAsceFogViewExtension>(GetWorld());
		if (ViewExtension.IsValid())
		{
			ViewExtension->SetSource(FogRawTex, FogSmoothTex, FIntPoint(GridW, GridH), CellSizeCm, WorldOrigin, FogDarkness);
			bFogSourceDirty = false;
		}
	}
	else if (bFogReady && bFogResourcesReady && ViewExtension.IsValid() && bFogSourceDirty)
	{
		ViewExtension->SetSource(FogRawTex, FogSmoothTex, FIntPoint(GridW, GridH), CellSizeCm, WorldOrigin, FogDarkness);
		bFogSourceDirty = false;
	}

	// drain 脏 tile（仅在 ViewExtension 就绪后，避免丢标记）
	if (bFogReady && ViewExtension.IsValid())
	{
		FlushDirtyTiles();
	}
}
