// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "AsceMass/Simulation/AsceTickContext.h"

#include "Subsystems/WorldSubsystem.h"
#include "AsceSpatialGridDefines.h"
#include "AsceSpatialGridSubsystem.generated.h"
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnGridQueryReadyDynamic, TArray<FMassEntityHandle>&, EntityHandles);
DECLARE_DELEGATE_OneParam(FOnGridQueryReady, TArray<FMassEntityHandle>&)

class UAsceGridEntitiesUpdateProcessor;
class UAsceGridBuildingsUpdateProcessor;
class UMassEntitySubsystem;
/**网格子系统
 * 主要用于记录和查询
 */
//注意，支持在processor里面使用多线程并行的查询和写入，但是不支持真正的异步线程，并行处理必须在这一帧立即返回
UCLASS(MinimalAPI)
class UAsceSpatialGridSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
	
public:
	UAsceSpatialGridSubsystem();

	// ── 查询实体，如果是在异步线程中，调用safe版本以在网格构建完成后返回回调
	UFUNCTION(BlueprintCallable)
	void BP_FindEntitiesSafe(const FVector2D& StartWorldLocation, const FVector2D& EndWorldLocation, FOnGridQueryReadyDynamic Callback);
	
	UFUNCTION(BlueprintCallable)
	void BP_FindEntitiesInRadiusSafe(const FVector2D& CenterWorldLocation, const float Radius, FOnGridQueryReadyDynamic Callback);
	
	UFUNCTION(BlueprintCallable)
	void BP_FindEntities(const FVector2D& StartWorldLocation, const FVector2D& EndWorldLocation, TArray<int64>& OutResult);
	
	UFUNCTION(BlueprintCallable)
	void BP_FindEntitiesInRadius(const FVector2D& CenterWorldLocation, const float Radius, TArray<int64>& OutResult);
	
	void FindEntitiesSafe(const FVector2D& StartWorldLocation, const FVector2D& EndWorldLocation, const FOnGridQueryReady& Callback);
	
	void FindEntitiesSafe(const FVector2D& CenterWorldLocation, const float Radius, const FOnGridQueryReady& Callback);
	
	void FindEntities(const FVector2D& StartWorldLocation, const FVector2D& EndWorldLocation, TArray<FMassEntityHandle>& OutResult);
	
	void FindEntities(const FVector2D& CenterWorldLocation, const float Radius, TArray<FMassEntityHandle>& OutResult);
	
	//传入屏幕空间的矩形两个顶点的坐标，查询被框选的实体（**单位 + 建筑**，按需开关）
	void FindEntitiesScreen(const FVector2D& StartScreenLocation, const FVector2D& EndScreenLocation, const APlayerController* Controller, TArray<FMassEntityHandle>& OutResult,
		bool bIncludeUnits = true, bool bIncludeBuildings = true);

	//传入屏幕空间的点坐标，使用这个点作为起点进行射线检测来选中实体（单位 + 建筑取离相机最近，按需开关）
	FMassEntityHandle FindEntitiesAtScreenPoint(const FVector2D ScreenLocation, float MaxDistance, APlayerController* Controller,
		bool bIncludeUnits = true, bool bIncludeBuildings = true);
	
	static UAsceSpatialGridSubsystem* Get(const UObject* WorldContextObject);
	/**
	 * 空间网格不会自动构建，而是需要手动调用进行构建, 在游戏的一开始和游戏中途都可以随时重新构建。网格大小根据最小实体半径来自动决定
	 * @param WorldStartLocation 开始构建的世界坐标
	 * @param WorldEndLocation 结束构建的世界坐标
	 * @param MinimalEntityRadius 最小实体单位半径
	 */
	UFUNCTION(BlueprintCallable)
	void ConstructGrid(const FVector2D WorldStartLocation, const FVector2D WorldEndLocation, const float MinimalEntityRadius = 75.f);

	/** **阶段入口**（由 `UAsceSimulationSubsystem` 调用；本系统已关闭自 tick）：
	 *  单位层重建 → 建筑层重建（processor 内部并行）→ 消费查询回调。 */
	void RunGridStages(const FAsceTickContext& Ctx);
	
	// ──────────────────────────────────────────
	// 建筑层（独立一份数据；坐标用**逻辑格**，粗格边长 = 逻辑格边长 × CoarseFactor）
	// ──────────────────────────────────────────

	/** 构建建筑网格（粗格覆盖整张逻辑地图）。`InCoarseFactor` 一般取 4（= costfield 边长 × 4）。 */
	UFUNCTION(BlueprintCallable)
	void ConstructBuildingsGrid(const FIntPoint LogicalGridDimension, const int32 InLogicalCellSize = 100, const int32 InCoarseFactor = 4);

	/** **线程安全**：把一个建筑写进它覆盖到的每个粗格（由建筑 processor 在并行 chunk 里调用）。 */
	void AddBuildingPart(const FAsceGridBuildingPart& Part);

	/** 建筑层是否已构建（`ConstructBuildingsGrid` 之后为真；建筑引导阶段等的就绪条件）。 */
	FORCEINLINE bool IsBuildingsGridReady() const { return BuildingCells.Num() > 0; }

	/** 建筑层的**逻辑格**尺寸（粗格数 × CoarseFactor；放置校验的越界判据）。 */
	FORCEINLINE FIntPoint GetBuildingsLogicalDim() const
	{
		return FIntPoint(BuildingsGridDim.X * CoarseFactor, BuildingsGridDim.Y * CoarseFactor);
	}

	/** 一个建筑占用的**逻辑格 AABB**（按 Rotation 绕中心旋转后；与登记口径一致）。 */
	bool GetBuildingLogicalBounds(const FAsceGridBuildingPart& Part, FIntPoint& OutMin, FIntPoint& OutMax) const;

	/** 收集与给定逻辑格 AABB **相交**的已有建筑（Entity 去重；放置校验用）。 */
	void FindBuildingsInLogicalRect(const FIntPoint& Min, const FIntPoint& Max, TArray<FMassEntityHandle>& OutBuildings) const;

	/**
	 * **射线查询（逻辑格空间）**：从 `FromLogicalCell` 出发、沿 `Dir`（AsceEther::EDir）、最多 `MaxLogicalCells` 格，
	 * 返回**最近**命中的建筑与"射线进入它 footprint 的那一格"。粗格为空则整格跳过（遍历量 ÷ CoarseFactor）。
	 */
	bool FindBuildingOnRay(const FIntPoint& FromLogicalCell, uint8 Dir, int32 MaxLogicalCells,
		FMassEntityHandle& OutBuilding, FIntPoint& OutEnterCell) const;

	/** 半径查询（世界坐标）：返回覆盖到的建筑（按 Entity 去重）。 */
	void FindBuildingsInRadius(const FVector2D& CenterWorldLocation, float Radius, TArray<FMassEntityHandle>& OutBuildings) const;

	const FAsceGridBuildingsData* GetBuildingsDataCell(const FIntPoint& CoarseCell) const;

	FORCEINLINE int32 GetBuildingsCoarseCellSize() const { return LogicalCellSize * CoarseFactor; }
	FORCEINLINE FIntPoint LogicalToCoarse(const FIntPoint& LogicalCell) const
	{
		return FIntPoint(LogicalCell.X / FMath::Max(1, CoarseFactor), LogicalCell.Y / FMath::Max(1, CoarseFactor));
	}
	FORCEINLINE bool IsValidBuildingCoord(const FIntPoint& CoarseCell) const
	{
		return CoarseCell.X >= 0 && CoarseCell.X < BuildingsGridDim.X
			&& CoarseCell.Y >= 0 && CoarseCell.Y < BuildingsGridDim.Y
			&& BuildingCells.Num() == BuildingsGridDim.X * BuildingsGridDim.Y;
	}
	FORCEINLINE int32 BuildingCoordToIndex(const FIntPoint& CoarseCell) const
	{
		return CoarseCell.X + BuildingsGridDim.X * CoarseCell.Y;
	}

	const FAsceSpatialGridEntitiesData* GetGridEntitiesDataCell(FIntVector2 GridLocation);
	
	//UTickableWorldSubsystem Interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual TStatId GetStatId() const override;
protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	//~UTickableWorldSubsystem Interface
	
	friend class UAsceGridEntitiesUpdateProcessor;
	friend class UAsceGridBuildingsUpdateProcessor;
	
	void InsertEntity(FMassEntityHandle EntityHandle, const FVector& Location, float Radius, float Height);
	
public:
	// 坐标映射
	FORCEINLINE FIntVector LocationToCoord(const FVector& Location) const
	{
		if (GridCellSize <= 0.f)
		{
			return FIntVector(-1, -1, 0);
		}

		const FVector Local(
			Location.X - WorldStart.X,
			Location.Y - WorldStart.Y,
			0.f
		);

		return FIntVector(
			FMath::FloorToInt(Local.X / GridCellSize),
			FMath::FloorToInt(Local.Y / GridCellSize),
			0
		);
	}
	
	FORCEINLINE bool IsValidCoord(const FIntVector& Coord) const{
		return Coord.X >= 0 && Coord.X < GridDimension.X
			&& Coord.Y >= 0 && Coord.Y < GridDimension.Y;
	}
	
	FORCEINLINE int32 CoordToIndex(const FIntVector& Coord) const
	{
		return Coord.X + GridDimension.X * Coord.Y;
	}
protected:
	// 重建网格实体信息
	void RebuildGridEntitiesData(const FAsceTickContext& Ctx);  

	// 重建"建筑层"（清格 + 并行跑建筑 processor 重填）
	void RebuildBuildingsData(const FAsceTickContext& Ctx);

	/** 建筑的粗格覆盖范围 / 写格（旋转后的世界 AABB → 粗格）。 */
	void GetPartCoarseRange(const FAsceGridBuildingPart& Part, FIntPoint& OutMin, FIntPoint& OutMax) const;
	void AddPartToCells(const FAsceGridBuildingPart& Part);
	
	// 查找范围内的实体
	void FindEntities_Internal(const FVector2D& StartWorldLocation, const FVector2D& EndWorldLocation, TArray<FMassEntityHandle>& OutHandles);
	void FindEntities_Internal(const FVector2D& CenterLocation, const float Radius, TArray<FMassEntityHandle>& OutHandles);
	
	//将屏幕坐标转换为世界射线
	void GetScreenCornerRays(const APlayerController* Controller, const FVector2D& MinScreen, const FVector2D& MaxScreen, FVector OutOrigins[4], FVector OutDirs[4]);
	//使用射线构建凸包
	FConvexVolume BuildFrustumFromCorners(const FVector NearCorners[4], const FVector FarCorners[4]);
	//投影视锥到水平面并得到所有的顶点
	TArray<FVector2D> ProjectFrustumToGround(const FVector NearCorners[4], const FVector FarCorners[4]);
	//使用2D平面上的点计算2D凸包
	TArray<FVector2D> ComputeConvexHull2D(const TArray<FVector2D>& Points);
	//获取2D凸包的AABB包围盒，我们不需要直接使用2D凸包与网格进行相交测试选出相交的网格，这部分的开销已经大于直接使用AABB框选的网格时多遍历的一些网格了
	void GetConvexHullAABB2D(const TArray<FVector2D>& Hull, FVector2D& OutMin, FVector2D& OutMax);
	//使用视锥对所有entity进行相交测试筛选
	void FindEntitiesInFrustum(const FConvexVolume& Frustum, const FVector2D& AABBMin, const FVector2D& AABBMax, TArray<FMassEntityHandle>& OutHandles) const;

	/** 射线命中的**最近单位**（世界射线；`OutT` 返回命中 t；未命中返回无效 handle）。 */
	FMassEntityHandle FindUnitHitOnRay(const FVector& Origin, const FVector& Dir, float MaxDistance, float& OutT);

	/** 射线命中的**最近建筑**（逻辑格 DDA + 粗格候选；命中盒 = footprint × HeightCm）。 */
	FMassEntityHandle FindBuildingHitOnRay(const FVector& Origin, const FVector& Dir, float MaxDistance, float& OutT) const;

	/** 框选：屏幕凸包 AABB 覆盖的建筑，盒 vs 视锥（按 Entity 去重追加）。 */
	void FindBuildingsInFrustum(const FConvexVolume& Frustum, const FVector2D& HullMin, const FVector2D& HullMax, TArray<FMassEntityHandle>& Out) const;

	//对AABB进行射线相交测试，返回首次交点的t值
	float RayAABBHit(const FVector& Origin, const FVector& Dir, const FVector& BoxCenter, const FVector& BoxExtent) const;
	
	//刷新格子的基础配置数据
	FORCEINLINE void RefreshBaseGridConfig()
	{
		GridCellSize = MinimalEntityRadius * 4;
		int DimensionX = FMath::CeilToInt(FMath::Abs(WorldEnd.X - WorldStart.X) / GridCellSize);
		int DimensionY = FMath::CeilToInt(FMath::Abs(WorldEnd.Y - WorldStart.Y) / GridCellSize);
		GridDimension = FIntVector2(DimensionX, DimensionY);
	}

public:
	// 网格构建的起点和终点
	UPROPERTY(BlueprintReadOnly)
	FVector2D WorldStart = FVector2D::ZeroVector;
	
	UPROPERTY(BlueprintReadOnly)
	FVector2D WorldEnd = FVector2D::ZeroVector;
	
	UPROPERTY(BlueprintReadOnly)
	float MinimalEntityRadius = 75.f;
	
	UPROPERTY(BlueprintReadOnly)
	float GridCellSize = 0;
	
	UPROPERTY()
	FIntVector2 GridDimension = FIntVector2::ZeroValue;
	
#if WITH_EDITOR
	static void DrawDebugFrustum(UWorld* World, const FConvexVolume& Frustum, const FVector NearCorners[4], const FVector FarCorners[4], const TArray<FVector2D>& GroundHull, const FVector& CameraLocation, float Duration = 2.f);
	static void DrawDebugSelectionRay(UWorld* World, const FVector& Origin, const FVector& Direction, float MaxDistance = 100000.f, float Duration = 2.f);
	
	bool bPaintDebugInfo = false;
	float DrawTime = 6.f;
#endif
	
private:
	//EntitiesData格子数据
	TArray<FAsceSpatialGridEntitiesData> EntitiesDataCells;

	UPROPERTY(Transient)
	TObjectPtr<UMassProcessor> GridEntitiesUpdateProcessor;

	// ── 建筑层 ──

	/** 建筑网格（粗格；尺寸 = ceil(逻辑格数 / CoarseFactor)）。 */
	TArray<FAsceGridBuildingsData> BuildingCells;
	FIntPoint BuildingsGridDim = FIntPoint::ZeroValue;

	/** 逻辑格边长（cm；与 footprint 的格一致，默认 100）。 */
	int32 LogicalCellSize = 100;

	/** 粗格 = 逻辑格边长 × CoarseFactor。 */
	int32 CoarseFactor = 4;

	UPROPERTY(Transient)
	TObjectPtr<UMassProcessor> GridBuildingsUpdateProcessor;
	
	struct FQueryCommand
	{
		FOnGridQueryReady Callback;
		FVector2D StartWorldLocation;
		FVector2D EndWorldLocation;
	};
	
	struct FRadiusQueryCommand
	{
		FOnGridQueryReady Callback;
		FVector2D CenterWorldLocation;
		float Radius;
	};
	
	//查询队列回调
	TArray<FQueryCommand> PendingQueryCommands;
	TArray<FRadiusQueryCommand> RadiusQueryCommands;
	
	
	TAtomic<bool> bIsRebuilding{ false };
};

template<>
struct TMassExternalSubsystemTraits<UAsceSpatialGridSubsystem>
{
	enum
	{
		GameThreadOnly = false,
	};
};