// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Subsystems/WorldSubsystem.h"

#include "AsceSpawnQueueSubsystem.generated.h"

/** 一条"生成实体"请求（技能弹道/生成物等）。
 *  - `DefRowName` 直接是**实体定义表**的行名（与 `UAsceEntityFactory::SpawnEntitiesByDef` 同一个表）；
 *  - `TeamId` 是**跨端一致**的 TeamId（子弹/生成物沿用发射者阵营）。 */
USTRUCT()
struct FAsceSpawnRequest
{
	GENERATED_BODY()

	UPROPERTY()
	FName DefRowName;

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	int32 TeamId = 0;
};

/**
 * 生成队列：**结算只入队，实体创建统一在帧末 drain**。
 *
 * 为什么必须排队：项目不变式"实体创建/销毁要排在模拟阶段 2（空间数据）之前"——
 * 战斗/技能结算发生在阶段 5b（战斗）之后，所以只能入队，等**帧末（阶段 8）**创建；
 * 这样下一帧的阶段 2 就会把新实体收进空间层/建筑层。见 `Combat/技能系统设计.md` §5。
 */
UCLASS()
class UAsceSpawnQueueSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** GameThread：入队一条生成请求（超过每帧上限会丢弃并告警，避免一次 AOE 打爆内存） */
	void Enqueue(const FAsceSpawnRequest& Request);

	/** 阶段 8（帧末）：drain 并按实体定义表创建（走 `UAsceEntityFactory`：阵营打包/贴地/def 数值与正常生成一致） */
	void RunGenerationStage();

	int32 GetNumPending() const { return PendingRequests.Num(); }

	/** 每帧入队上限（超了丢弃 + 告警一次） */
	static constexpr int32 MaxRequestsPerFrame = 256;

private:
	TArray<FAsceSpawnRequest> PendingRequests;
	bool bWarnedOverflow = false;
};
