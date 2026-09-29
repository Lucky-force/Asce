// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

/**
 * "按距离的更新间隔"（近快远慢）：给**连续变化**的通道（位置/速度）做降频用的。见 `Networking/Mass复制设计.md` §9。
 *
 * 为什么：
 *  - 连续量（位置）远端的实体不需要像近处那样频繁更新；用距离分档把远端压到低频，能实打实**省带宽**。
 *  - 近处保持高频（观感），远处低频（够用即可）。
 *
 * 门控顺序（在 `SetXxxFromFragment` 里）：**先判 interval**（没到点 ⇒ 不写、不 stamp），再走"公平分帧 + 硬预算"。
 * 没通过 interval 的项不算"本轮已服务"，下轮仍有机会。
 *
 * 与 LOD 无关：直接按"到该客户端 viewer 的距离"分档（viewer = 服务器那份 observer Pawn，
 * 被客户端相机焦点驱动，见 `AAscePlayerController::Server_UpdateViewFocus`）。
 */
namespace UE::AsceMass::Replication
{
	/** 距离分档（用 **DistSquared** 比较，避免 sqrt）与对应更新间隔（秒）。数值实跑后可调。 */
	struct FAsceReplicationIntervalBands
	{
		/** 分档边界（cm²）：近 / 中 / 远，超过远档用 OffInterval */
		float NearDistSq = 3000.f * 3000.f;
		float MidDistSq = 8000.f * 8000.f;
		float FarDistSq = 16000.f * 16000.f;

		/** 各档的更新间隔（秒）：近 ≈每帧 / ~20Hz / ~3Hz / ~1Hz。
		 *  注意近档 0.01s < 一帧（60fps≈0.0167s）⇒ 实际就是"每帧都发"（不节流）。 */
		float NearInterval = 0.01f;
		float MidInterval = 0.05f;
		float FarInterval = 0.3f;
		float OffInterval = 1.0f;
	};

	inline const FAsceReplicationIntervalBands& GetReplicationIntervalBands()
	{
		static const FAsceReplicationIntervalBands Bands;
		return Bands;
	}

	/** 距离（平方）→ 更新间隔（秒）。 */
	FORCEINLINE float ComputeIntervalFromDistSq(const float DistSq)
	{
		const FAsceReplicationIntervalBands& Bands = GetReplicationIntervalBands();
		if (DistSq < Bands.NearDistSq) { return Bands.NearInterval; }
		if (DistSq < Bands.MidDistSq) { return Bands.MidInterval; }
		if (DistSq < Bands.FarDistSq) { return Bands.FarInterval; }
		return Bands.OffInterval;
	}

	/**
	 * 解析某个客户端 bubble 对应的 viewer 位置（= 服务器那份 observer Pawn，被该客户端的相机焦点驱动）。
	 * bubble actor 的 Owner 就是对应 PlayerController（见 `UAsceUnitReplicator` 的 ResolveClientTeamKey 同款做法）。
	 * 拿不到（未就绪）返回 false ⇒ 调用方按"不限频"处理。
	 */
	FORCEINLINE bool ResolveViewerLocationFromBubbleOwner(const AActor* BubbleActor, FVector& OutLocation)
	{
		const APlayerController* PC = BubbleActor ? Cast<APlayerController>(BubbleActor->GetOwner()) : nullptr;
		const APawn* ViewerPawn = PC ? PC->GetPawn() : nullptr;
		if (!ViewerPawn)
		{
			return false;
		}
		OutLocation = ViewerPawn->GetActorLocation();
		return true;
	}
} // namespace UE::AsceMass::Replication
