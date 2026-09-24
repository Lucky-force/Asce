// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceSpawnQueueSubsystem.h"

#include "AsceMass/Data/AsceEntityFactory.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceSpawnQueueSubsystem)

void UAsceSpawnQueueSubsystem::Enqueue(const FAsceSpawnRequest& Request)
{
	check(IsInGameThread());

	if (Request.DefRowName.IsNone())
	{
		return;   // 没配生成物行名（技能配置问题）：静默跳过
	}
	if (PendingRequests.Num() >= MaxRequestsPerFrame)
	{
		if (!bWarnedOverflow)
		{
			bWarnedOverflow = true;
			UE_LOG(LogTemp, Warning, TEXT("[Spawn] 本帧生成请求超过上限 %d，后续请求被丢弃"), MaxRequestsPerFrame);
		}
		return;
	}
	PendingRequests.Add(Request);
}

void UAsceSpawnQueueSubsystem::RunGenerationStage()
{
	if (PendingRequests.Num() == 0)
	{
		return;
	}

	// 先换出来再创建：保持"drain 期间不会再被追加"的不变量清晰
	TArray<FAsceSpawnRequest> Requests = MoveTemp(PendingRequests);
	PendingRequests.Reset();
	bWarnedOverflow = false;

	for (const FAsceSpawnRequest& Request : Requests)
	{
		const TArray<FTransform> Transforms{ Request.Transform };
		UAsceEntityFactory::SpawnEntitiesByDef(this, Request.DefRowName, Transforms, Request.TeamId);
	}
}
