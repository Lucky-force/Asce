// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "MassEntityTemplate.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"   // 阵营列常量/换算（AsceMaxTeams/AsceNeutralTeamId/AsceTeamIndex）
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

// ── 阵营列 ──
// 0 = 无阵营（`FAsceTeamFragment::TeamKey` 的默认值；资源这类中立物也是 0，**可被攻击**）；
// 1..AsceMaxTeams-1 = 战斗阵营。子弹沿用生成者的阵营。
// `AsceMaxTeams` / `AsceNeutralTeamId` / `AsceTeamIndex` 定义在 `Fragments/AsceCoreFragments.h`（阵营属性所在地，供非空间层代码复用）。

// ── 单个格子中的实体信息 ──
struct FAsceSpatialGridEntitiesData
{
	// 按阵营分列存：扫描敌人时整列跳过友军，不必逐条比较阵营。
	// 外层固定 AsceMaxTeams 列（inline，不产生堆分配），每列按需分配。
	// Reset 只清空不释放容量 ⇒ 每帧重建（RebuildGridEntitiesData）不会反复申请内存。
	TArray<TArray<FAsceGridEntityData>, TInlineAllocator<AsceMaxTeams>> EntitiesByTeam;

	mutable TAtomic<bool> LockFlag { false };  // 自旋锁

	FAsceSpatialGridEntitiesData()
	{
		EntitiesByTeam.SetNum(AsceMaxTeams);
	}

	void Reset()
	{
		for (TArray<FAsceGridEntityData>& List : EntitiesByTeam)
		{
			List.Reset();
		}
	}
	
	FORCEINLINE void Lock()
	{
		while (LockFlag.Exchange(true));
	}

	FORCEINLINE void Unlock()
	{
		LockFlag = false;
	}

	FORCEINLINE void AddEntity(int32 TeamId, const FAsceGridEntityData& Entity)
	{
		Lock();
		EntitiesByTeam[TeamId].Add(Entity);
		Unlock();
	}

	/** 该格实体总数（调试/统计用） */
	FORCEINLINE int32 GetNumEntities() const
	{
		int32 Count = 0;
		for (const TArray<FAsceGridEntityData>& List : EntitiesByTeam)
		{
			Count += List.Num();
		}
		return Count;
	}

	/** 遍历该格**所有队**的实体（避让/碰撞/框选/射线/调试等"要所有单位"的调用方） */
	template <typename FnType>
	FORCEINLINE void ForEachEntity(FnType&& Fn) const
	{
		for (const TArray<FAsceGridEntityData>& List : EntitiesByTeam)
		{
			for (const FAsceGridEntityData& Entity : List)
			{
				Fn(Entity);
			}
		}
	}

	/** 只遍历**指定阵营列**（框选只收自己方用：比全列遍历少 N-1 次空检查） */
	template <typename FnType>
	FORCEINLINE void ForEachEntityInTeam(int32 TeamId, FnType&& Fn) const
	{
		for (const FAsceGridEntityData& Entity : EntitiesByTeam[TeamId])
		{
			Fn(Entity);
		}
	}

	/** 只遍历**非我队**的实体（战斗扫描用；0 号无阵营对战斗单位也算敌人 ⇒ 只跳过 MyTeamId 列） */
	template <typename FnType>
	FORCEINLINE void ForEachEnemy(int32 MyTeamId, FnType&& Fn) const
	{
		for (int32 TeamId = 0; TeamId < AsceMaxTeams; ++TeamId)
		{
			if (TeamId == MyTeamId)
			{
				continue;
			}
			for (const FAsceGridEntityData& Entity : EntitiesByTeam[TeamId])
			{
				Fn(Entity);
			}
		}
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

	/** 选择/命中盒高度（cm；来自 footprint 的 `OcclusionHeightCm`，至少 100 保证盒非退化）。 */
	int32 HeightCm = 100;

	/** 基座 Z（cm；建筑贴地后 = 实体 Transform 的 Z；选择盒从这个高度起算）。 */
	float BaseZ = 0.f;

	/** **本端**阵营列（= `AAsceGameState::FindTeamIndex(AsceTeamId(实体 TeamKey))`；选择/战斗按阵营过滤用）。
	 *  建筑**不分列**（Ether 射线逐格查是热点，分列会让每次查格 ×N），这里只缓存一字节、查询时比较一次。 */
	uint8 TeamIndex = AsceNeutralTeamId;
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

