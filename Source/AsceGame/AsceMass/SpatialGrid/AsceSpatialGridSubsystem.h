// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "AsceSpatialGridDefines.h"
#include "AsceSpatialGridSubsystem.generated.h"
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnGridQueryReadyDynamic, TArray<FMassEntityHandle>&, EntityHandles);
DECLARE_DELEGATE_OneParam(FOnGridQueryReady, TArray<FMassEntityHandle>&)

class UAsceGridEntitiesUpdateProcessor;
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
	
	//传入屏幕空间的矩形两个顶点的坐标，查询被框选的实体
	void FindEntitiesScreen(const FVector2D& StartScreenLocation, const FVector2D& EndScreenLocation, const APlayerController* Controller, TArray<FMassEntityHandle>& OutResult);

	//传入屏幕空间的点坐标，使用这个点作为起点进行射线检测来选中实体
	FMassEntityHandle FindEntitiesAtScreenPoint(const FVector2D ScreenLocation, float MaxDistance, APlayerController* Controller);
	
	static UAsceSpatialGridSubsystem* Get(const UObject* WorldContextObject);
	/**
	 * 空间网格不会自动构建，而是需要手动调用进行构建, 在游戏的一开始和游戏中途都可以随时重新构建。网格大小根据最小实体半径来自动决定
	 * @param WorldStartLocation 开始构建的世界坐标
	 * @param WorldEndLocation 结束构建的世界坐标
	 * @param MinimalEntityRadius 最小实体单位半径
	 */
	UFUNCTION(BlueprintCallable)
	void ConstructGrid(const FVector2D WorldStartLocation, const FVector2D WorldEndLocation, const float MinimalEntityRadius = 75.f);
	
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
	void RebuildGridEntitiesData(float DeltaTime);  
	
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
	//对AABB进行射线相交测试，返回首次交点的t值
	float RayAABBHit(const FVector& Origin, const FVector& Dir, const FVector& BoxCenter, const FVector& BoxExtent);
	
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