// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "AsceAnimDef.generated.h"

class USkeleton;

/** 保留 clip 名（回退链末端；每个 AnimSet 都应至少配其中几个）。见 `技能系统设计.md` §7。 */
namespace AsceAnimClipNames
{
	static const FName Idle("Idle");
	static const FName Move("Move");
	static const FName Attack("Attack");
	static const FName Dying("Dying");
	static const FName CastGeneric("Cast_Generic");
	static const FName ChannelGeneric("Channel_Generic");
}

/**
 * 一段烘焙好的动画（Bone 模式）。
 *
 * 帧语义与 AnimToTexture 一致：`StartFrame/EndFrame` 是**烘焙贴图内的绝对帧**（直接从插件的
 * AnimToTexture DataAsset 的 `Animations[]` 条目抄，区间可以比单段动画大，比如 Idle/Move 共用一张图）；
 * `AnimIndex` 只作元数据/调试（播放本身只用绝对帧区间）。
 */
USTRUCT(BlueprintType)
struct FAsceAnimClip
{
	GENERATED_BODY()

	/** 烘焙里的动画序号（AnimToTexture DataAsset 的 `Animations` 数组下标；元数据/调试用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Anim")
	int32 AnimIndex = 0;

	/** 段起始帧（烘焙贴图内的**绝对帧**） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Anim")
	int32 StartFrame = 0;

	/** 段结束帧（烘焙贴图内的**绝对帧**；`bLoop` 时在 [Start, End) 内循环，否则播到 End 定格） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Anim")
	int32 EndFrame = 0;

	/** 循环（Idle/Move/Channel = true；Attack/Casting = false） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Anim")
	bool bLoop = false;

	/** 基准播放速度（1.0x 对应值；实际 = 服务器 PlayRate，或 PlayRate / BasePlayRate） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asce|Anim")
	float BasePlayRate = 1.f;
};

/**
 * 每个**骨架**一份的动画集（Bone 模式：同骨架下所有 mesh 共用一份烘焙）。
 *
 * - `Clips` 键 = 语义 clip 名（技能 `FAsceActionDef::AnimClips` / 保留名 `AsceAnimClipNames`）；
 * - 解析回退链：技能专属 clip → Cast_Generic/Channel_Generic/Attack → Idle → 不播；
 * - 单位类型到本资产的映射：单位 def 表的 `AnimSet` 字段（客户端通过复制的 `UniqueTypeId` 查表解析）。
 */
UCLASS(BlueprintType)
class UAsceAnimSet : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 本动画集对应的骨架（同骨架可共用一份烘焙） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce|Anim")
	TSoftObjectPtr<USkeleton> Skeleton;

	/** Bone 模式烘焙数据（AnimToTexture 插件的 DataAsset；插件启用前先用 UObject 软引用占位） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce|Anim")
	TSoftObjectPtr<UObject> BoneData;

	/** 烘焙帧率（帧/秒；表现 processor 用它把"经过时间"换算成帧号） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce|Anim")
	float FrameRate = 30.f;

	/** clip 名 → 烘焙段 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce|Anim")
	TMap<FName, FAsceAnimClip> Clips;
};
