// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "MassEntityTemplate.h"
#include "AsceSpatialGridDefines.generated.h"

//格子中存储的实体信息, 目前来说只需要存一个在这个格子中的EntityHandle即可
struct FAsceGridEntityData
{
	//Entity的handle，直接获取格子中的entity数据
	FMassEntityHandle EntityHandle;
	
	//Entity的缓存真实坐标，用来快速计算避障，避免缓存未命中开销
	FVector CacheEntityLocation = FVector::ZeroVector;
	
	//Entity的半径，同样用来快速计算避障
	float CachedEntityRadius = 0.0f;
	
	//Entity的高度，快速计算筛选
	float CachedEntityHeight = 0.0f;
};

// ── 单个格子中的实体信息 ──
struct FAsceSpatialGridEntitiesData
{
	//这个格子中的所有Entity数据
	TArray<FAsceGridEntityData, TInlineAllocator<16>> Entities;
	
	mutable TAtomic<bool> LockFlag { false };  // 自旋锁

	void Reset() { Entities.Reset(); }
	
	FORCEINLINE void Lock()
	{
		while (LockFlag.Exchange(true));
	}

	FORCEINLINE void Unlock()
	{
		LockFlag = false;
	}

	FORCEINLINE void AddEntity(const FAsceGridEntityData& Entity)
	{
		Lock();
		Entities.Add(Entity);
		Unlock();
	}
};


//一个空间网格的基础信息, 可以有多个基础信息来叠加出网格的最终信息
USTRUCT(BlueprintType)
struct FAsceSpatialGridBaseData
{
	GENERATED_BODY()
	
	//网格的高度，高度不会造成任何寻路问题，单位是无视高度进行移动的，高度只处理视野问题
	uint8 Height = 0;
	
	//此网格的代价
	uint8 Cost = 1;
};


// ──────────────────────────────────────────
// 建筑层（独立一份数据；坐标用**逻辑格**，粗格边长 = 逻辑格边长 × CoarseFactor）
// ──────────────────────────────────────────

/** 建筑在建筑网格里的一份登记（多格建筑会出现在它覆盖到的每个粗格里）。 */
struct FAsceGridBuildingPart
{
	FMassEntityHandle Entity;

	/** 逻辑格（与 footprint 同一口径：**旋转后的最终 AABB**；costfield/视野层也按这个矩形遍历）。 */
	FIntPoint Origin = FIntPoint::ZeroValue;
	FIntPoint Size = FIntPoint(1, 1);
};

/** 建筑网格的一格：一格可能覆盖多个（小）建筑。 */
struct FAsceGridBuildingsData
{
	TArray<FAsceGridBuildingPart, TInlineAllocator<4>> Buildings;

	mutable TAtomic<bool> LockFlag { false };  // 自旋锁（并行插入同一粗格时用）

	void Reset() { Buildings.Reset(); }

	FORCEINLINE void Lock()
	{
		while (LockFlag.Exchange(true));
	}

	FORCEINLINE void Unlock()
	{
		LockFlag = false;
	}

	FORCEINLINE void AddBuilding(const FAsceGridBuildingPart& Part)
	{
		Lock();
		Buildings.Add(Part);
		Unlock();
	}
};

