// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceSpatialGridSubsystem.h"

#include "AsceGridEntitiesUpdateProcessor.h"
#include "AsceGridBuildingsUpdateProcessor.h"
#include "AsceSpatialGridDefines.h"
#include "MassEntitySubsystem.h"
#include "Engine/World.h"
#include "MassEntityTypes.h"
#include "MassExecutor.h"
#include "MassObserverNotificationTypes.h"
#include "MassProcessingContext.h"
#include "Kismet/KismetMathLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceSpatialGridSubsystem)

void UAsceSpatialGridSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UAsceSpatialGridSubsystem::Tick(float DeltaTime)
{
    // 自 tick 已停用：本 tick 体已挪成 `RunGridStages`，由 `UAsceSimulationSubsystem` 按阶段顺序调用
    // （必须排在 Ether 的 drain 之前，保证 Ether 建图时读到的空间数据是本轮最新的）。
    Super::Tick(DeltaTime);
}

void UAsceSpatialGridSubsystem::RunGridStages(const FAsceTickContext& Ctx)
{
    // 1. 重建网格
    bIsRebuilding = true;
    RebuildGridEntitiesData(Ctx);
    RebuildBuildingsData(Ctx);
    bIsRebuilding = false;

    // 2. 消费所有回调（线段/盒版）
    // ⚠️ TODO(safe-query)：半径版对应的 `RadiusQueryCommands` 这里**没有**消费（见头文件 TODO）⇒
    // 半径 safe 查询回调永不触发 + 队列泄漏；目前无调用方，待决策修法。
    if (PendingQueryCommands.Num() > 0)
    {
        TArray<FMassEntityHandle> EntityHandles;
        for (FQueryCommand& Command : PendingQueryCommands)
        {
            EntityHandles.Reset();
            FindEntities_Internal(Command.StartWorldLocation, Command.EndWorldLocation, EntityHandles);
            Command.Callback.ExecuteIfBound(EntityHandles);
        }
        PendingQueryCommands.Empty();
    }
}

void UAsceSpatialGridSubsystem::RebuildGridEntitiesData(const FAsceTickContext& Ctx)
{
    if (EntitiesDataCells.Num() == 0 || !GridEntitiesUpdateProcessor)
    {
        return;
    }
    
    // 清空所有格子
    for (auto& Cell : EntitiesDataCells)
    {
        Cell.Reset();
    }

    // 按需执行 Processor（同步完成，支持并行）；MassEntity 子系统/实体管理器从上下文取
    if (Ctx.EntityManager)
    {
        FMassProcessingContext ProcessingContext(*Ctx.EntityManager, Ctx.DeltaTime);
        UE::Mass::Executor::Run(*GridEntitiesUpdateProcessor, ProcessingContext);
    }
}

bool UAsceSpatialGridSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    // 只在 GameWorld / PIE 创建，不在编辑器预览世界创建
    UWorld* World = Cast<UWorld>(Outer);
    if (!World)
    {
        return false;
    }
    if (!Super::ShouldCreateSubsystem(Outer))
    {
        return false;
    }
    return World->IsGameWorld() || World->IsPlayInEditor();
}

void UAsceSpatialGridSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    // Grid 由 ConstructGrid 显式构建，此处不自动初始化
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return;
    }
    UMassEntitySubsystem* EntitySubsys = World->GetSubsystem<UMassEntitySubsystem>();
    if (EntitySubsys)
    {
        FMassEntityManager& EntityManager = EntitySubsys->GetMutableEntityManager();
        EntityManager.Initialize();
        GridEntitiesUpdateProcessor = NewObject<UAsceGridEntitiesUpdateProcessor>(this);
        GridEntitiesUpdateProcessor->CallInitialize(this, EntityManager.AsShared());
        GridBuildingsUpdateProcessor = NewObject<UAsceGridBuildingsUpdateProcessor>(this);
        GridBuildingsUpdateProcessor->CallInitialize(this, EntityManager.AsShared());
    }
}

void UAsceSpatialGridSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
    EntitiesDataCells.Empty();
    BuildingCells.Empty();
    BuildingsGridDim = FIntPoint::ZeroValue;
    WorldStart = FVector2D::ZeroVector;
    WorldEnd   = FVector2D::ZeroVector;
    GridDimension = FIntVector2::ZeroValue;
    GridCellSize = 0.f;
    Super::OnWorldEndPlay(InWorld);
}

TStatId UAsceSpatialGridSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UAsceSpatialGridSubsystem, STATGROUP_Tickables);
}

UAsceSpatialGridSubsystem::UAsceSpatialGridSubsystem()
{
    
}

void UAsceSpatialGridSubsystem::BP_FindEntitiesSafe(const FVector2D& StartWorldLocation, const FVector2D& EndWorldLocation, FOnGridQueryReadyDynamic Callback)
{
    FindEntitiesSafe(StartWorldLocation, EndWorldLocation, FOnGridQueryReady::CreateWeakLambda(this,[Callback](TArray<FMassEntityHandle>& EntityHandles)
    {
        Callback.ExecuteIfBound(EntityHandles);
    }));
}

void UAsceSpatialGridSubsystem::BP_FindEntitiesInRadiusSafe(const FVector2D& CenterWorldLocation, const float Radius, FOnGridQueryReadyDynamic Callback)
{
    FindEntitiesSafe(CenterWorldLocation, Radius, FOnGridQueryReady::CreateWeakLambda(this,[Callback](TArray<FMassEntityHandle>& EntityHandles)
   {
       Callback.ExecuteIfBound(EntityHandles);
   }));
}

void UAsceSpatialGridSubsystem::BP_FindEntities(const FVector2D& StartWorldLocation, const FVector2D& EndWorldLocation, TArray<int64>& OutResult)
{
    OutResult.Reset();
    TArray<FMassEntityHandle> EntityHandles;
    FindEntities(StartWorldLocation, EndWorldLocation, EntityHandles);
    for (FMassEntityHandle EntityHandle : EntityHandles)
    {
        OutResult.Add(EntityHandle.AsNumber());
    }
}

void UAsceSpatialGridSubsystem::BP_FindEntitiesInRadius(const FVector2D& CenterWorldLocation, const float Radius, TArray<int64>& OutResult)
{
    OutResult.Reset();
    TArray<FMassEntityHandle> EntityHandles;
    FindEntities(CenterWorldLocation, Radius, EntityHandles);
    for (FMassEntityHandle EntityHandle : EntityHandles)
    {
        OutResult.Add(EntityHandle.AsNumber());
    }
}

void UAsceSpatialGridSubsystem::FindEntitiesSafe(const FVector2D& StartWorldLocation, const FVector2D& EndWorldLocation, const FOnGridQueryReady& Callback)
{
    if (bIsRebuilding)
    {
        //缓存, 如果是在主线程查询，那么必定不会走到这里
        PendingQueryCommands.Add({Callback, StartWorldLocation, EndWorldLocation});
    }
    else
    {
        //直接执行
        TArray<FMassEntityHandle> EntityHandles;
        FindEntities_Internal(StartWorldLocation, EndWorldLocation, EntityHandles);
        Callback.ExecuteIfBound(EntityHandles);
    }
}

void UAsceSpatialGridSubsystem::FindEntitiesSafe(const FVector2D& CenterWorldLocation, const float Radius, const FOnGridQueryReady& Callback)
{
    if (bIsRebuilding)
    {
        // ⚠️ TODO(safe-query)：这个队列**没有消费者**（`RunGridStages` 只处理 `PendingQueryCommands`）⇒
        // 走到这里意味着回调永远不会触发（调用方若在等回调会悬挂），而且队列只增不减（泄漏）。
        // 现在没有任何调用方；修法见头文件 `TODO(safe-query)`（比对线段版的同帧消费补齐 / 删 API / 做真线程安全）。
        RadiusQueryCommands.Add({Callback, CenterWorldLocation, Radius});
    }
    else
    {
        //直接执行
        TArray<FMassEntityHandle> EntityHandles;
        FindEntities_Internal(CenterWorldLocation, Radius, EntityHandles);
        Callback.ExecuteIfBound(EntityHandles);
    }
}

void UAsceSpatialGridSubsystem::FindEntities(const FVector2D& StartWorldLocation, const FVector2D& EndWorldLocation, TArray<FMassEntityHandle>& OutResult)
{
    FindEntities_Internal(StartWorldLocation, EndWorldLocation, OutResult);
}

void UAsceSpatialGridSubsystem::FindEntities(const FVector2D& CenterWorldLocation, const float Radius, TArray<FMassEntityHandle>& OutResult)
{
    FindEntities_Internal(CenterWorldLocation, Radius, OutResult);
}

void UAsceSpatialGridSubsystem::FindEntitiesScreen(const FVector2D& StartScreenLocation, const FVector2D& EndScreenLocation, const APlayerController* Controller, TArray<FMassEntityHandle>& OutResult,
    bool bIncludeUnits, bool bIncludeBuildings)
{
    // 1. 规范化屏幕矩形 (处理反向拖拽)
    const FVector2D MinScreen(
        FMath::Min(StartScreenLocation.X, EndScreenLocation.X),
        FMath::Min(StartScreenLocation.Y, EndScreenLocation.Y));
    const FVector2D MaxScreen(
        FMath::Max(StartScreenLocation.X, EndScreenLocation.X),
        FMath::Max(StartScreenLocation.Y, EndScreenLocation.Y));

    // 2. 获取 4 条角点射线
    FVector RayOrigins[4];
    FVector RayDirs[4];
    GetScreenCornerRays(Controller, MinScreen, MaxScreen, RayOrigins, RayDirs);

    // 3. 计算近/远平面角点
    const float NearClip = 10.f;
    FVector NearCorners[4];
    FVector FarCorners[4];
    for (int32 i = 0; i < 4; ++i)
    {
        NearCorners[i] = RayOrigins[i] + RayDirs[i] * NearClip;
    }
    
    for (int32 i = 0; i < 4; ++i)
    {
        const float t = (RayDirs[i].Z != 0.f)
            ? -RayOrigins[i].Z / RayDirs[i].Z
            : TNumericLimits<float>::Max();
        FarCorners[i] = RayOrigins[i] + RayDirs[i] * FMath::Max(t, NearClip + 1.f);
    }

    // 4. 构建视锥体
    FConvexVolume Frustum = BuildFrustumFromCorners(NearCorners, FarCorners);


    // 5. 投影到地平面 → 2D 凸包
    TArray<FVector2D> GroundHull = ProjectFrustumToGround(NearCorners, FarCorners);

        
#if WITH_EDITOR
    if (bPaintDebugInfo)
    {
        DrawDebugFrustum(Controller->GetWorld(), Frustum, NearCorners, FarCorners, GroundHull, Controller->PlayerCameraManager->GetCameraLocation(), DrawTime);
    }    
#endif
    
    // 6. 凸包 AABB → 空间网格粗筛
    FVector2D HullMin, HullMax;
    GetConvexHullAABB2D(GroundHull, HullMin, HullMax);

    // 7. 视锥-AABB 精筛（单位 + 建筑，按需开关）
    OutResult.Reset();
    if (bIncludeUnits)
    {
        FindEntitiesInFrustum(Frustum, HullMin, HullMax, OutResult);
    }
    if (bIncludeBuildings)
    {
        FindBuildingsInFrustum(Frustum, HullMin, HullMax, OutResult);
    }
}

FMassEntityHandle UAsceSpatialGridSubsystem::FindUnitHitOnRay(const FVector& Origin, const FVector& Dir, float MaxDistance, float& OutT)
{
    OutT = TNumericLimits<float>::Max();

    //起点网格
    FIntVector OriginCell = LocationToCoord(Origin);

    //步长
    const int32 StepX = (Dir.X > 0) ? 1 : -1;
    const int32 StepY = (Dir.Y > 0) ? 1 : -1;
    
    //下一个边界
    const float NextBoundaryX = WorldStart.X + (OriginCell.X + (StepX > 0 ? 1 : 0)) * GridCellSize;
    const float NextBoundaryY = WorldStart.Y + (OriginCell.Y + (StepY > 0 ? 1 : 0)) * GridCellSize;

    //到达下一个边界所需要的时间
    float tMaxX = (FMath::Abs(Dir.X) > SMALL_NUMBER) ? (NextBoundaryX - Origin.X) / Dir.X : TNumericLimits<float>::Max();
    float tMaxY = (FMath::Abs(Dir.Y) > SMALL_NUMBER) ? (NextBoundaryY - Origin.Y) / Dir.Y : TNumericLimits<float>::Max();

    //每次到达下一个边界需要的时间
    const float tDeltaX = (FMath::Abs(Dir.X) > SMALL_NUMBER) ? GridCellSize / FMath::Abs(Dir.X) : TNumericLimits<float>::Max();
    const float tDeltaY = (FMath::Abs(Dir.Y) > SMALL_NUMBER) ? GridCellSize / FMath::Abs(Dir.Y) : TNumericLimits<float>::Max();

    FMassEntityHandle Closest;
    float ClosestT = MaxDistance;
    TSet<FMassEntityHandle> Visited;
#if WITH_EDITOR
    if (bPaintDebugInfo)
    {
        DrawDebugSelectionRay(GetWorld(), FVector::ZeroVector/*Origin*/, Dir, 100000.f, DrawTime);
    }
#endif
    // 2. 遍历穿过的格子 (DDA)
    // 步数按 MaxDistance 推（原来是写死 200 步 ⇒ 只覆盖 200*GridCellSize，默认 300cm 即 600m；
    // 超过这个距离的单位点不中，而同距离的建筑能选中 ⇒ 两条路径射程不一致）
    const int32 MaxSteps = (GridCellSize > 0.f)
        ? FMath::Clamp(FMath::CeilToInt(MaxDistance / GridCellSize) + 8, 1, 4096)
        : 200;
    for (int32 Steps = 0; Steps < MaxSteps; ++Steps)
    {
        if (IsValidCoord(OriginCell))
        {
            // 3. 格子内实体做射线-球体 (3D, 不是水平面)
            for (auto& E : EntitiesDataCells[CoordToIndex(OriginCell)].Entities)
            {
                if (Visited.Contains(E.EntityHandle))
                {
                    continue;
                }
                Visited.Add(E.EntityHandle);

                // 命中盒：用实体的**真实 Z** + **高度片段**（和框选/debugger 同一口径；高度缺失时退回用半径）
                const float PickHeight = (E.CachedEntityHeight > 0.f) ? E.CachedEntityHeight : E.CachedEntityRadius;
                float T = RayAABBHit(Origin, Dir, FVector(E.CacheEntityLocation.X, E.CacheEntityLocation.Y, E.CacheEntityLocation.Z + PickHeight * 0.5f), FVector(E.CachedEntityRadius, E.CachedEntityRadius, PickHeight * 0.5f));
                if (T > 0 && T < ClosestT)
                {
                    ClosestT = T;
                    Closest = E.EntityHandle;
                }
            }
        }

        if (Closest.IsValid())
        {
            break;
        }
        
        // 4. DDA 步进, 谁更快到达下一个边界，直接步进到边界，然后计算步进到下一个边界的时间
        if (tMaxX < tMaxY)
        {
            OriginCell.X += StepX; 
            tMaxX += tDeltaX;
        }
        else
        {
            OriginCell.Y += StepY; 
            tMaxY += tDeltaY;
        }
        
        if (FMath::Min(tMaxX, tMaxY) > MaxDistance)
        {
            break;
        }
    }

    if (Closest.IsValid())
    {
        OutT = ClosestT;
    }
    return Closest;
}

FMassEntityHandle UAsceSpatialGridSubsystem::FindEntitiesAtScreenPoint(const FVector2D ScreenLocation, float MaxDistance, APlayerController* Controller,
    bool bIncludeUnits, bool bIncludeBuildings)
{
    if (!IsValid(Controller))
    {
        return FMassEntityHandle();
    }

    FVector Origin, Dir;
    if (!Controller->DeprojectScreenPositionToWorld(ScreenLocation.X, ScreenLocation.Y, Origin, Dir))
    {
        return FMassEntityHandle();
    }
    Dir.Normalize();

    // 单位与建筑各算一次最近命中，取离相机更近的那个
    FMassEntityHandle Best;
    float BestT = TNumericLimits<float>::Max();

    if (bIncludeUnits)
    {
        float T = TNumericLimits<float>::Max();
        const FMassEntityHandle Hit = FindUnitHitOnRay(Origin, Dir, MaxDistance, T);
        if (Hit.IsValid() && T < BestT)
        {
            Best = Hit;
            BestT = T;
        }
    }
    if (bIncludeBuildings)
    {
        float T = TNumericLimits<float>::Max();
        const FMassEntityHandle Hit = FindBuildingHitOnRay(Origin, Dir, MaxDistance, T);
        if (Hit.IsValid() && T < BestT)
        {
            Best = Hit;
            BestT = T;
        }
    }
    return Best;
}

    FMassEntityHandle UAsceSpatialGridSubsystem::FindBuildingHitOnRay(const FVector& Origin, const FVector& Dir, float MaxDistance, float& OutT) const
{
    // OutT 用 MaxDistance 播种（与 `FindUnitHitOnRay` 的 `ClosestT` 同口径）：命中必须落在射程内
    OutT = MaxDistance;
    if (BuildingCells.Num() == 0 || LogicalCellSize <= 0)
    {
        return FMassEntityHandle();
    }

    // 起点逻辑格（世界 → 逻辑格，与粗格换算同一口径）
    FIntPoint Cell(
        FMath::FloorToInt(Origin.X / LogicalCellSize),
        FMath::FloorToInt(Origin.Y / LogicalCellSize));
    const FIntPoint LogicalDim = GetBuildingsLogicalDim();

    // 2D DDA（逻辑格；Z 不参与步进，盒测试是 3D）
    const int32 StepX = (Dir.X > 0.f) ? 1 : ((Dir.X < 0.f) ? -1 : 0);
    const int32 StepY = (Dir.Y > 0.f) ? 1 : ((Dir.Y < 0.f) ? -1 : 0);

    auto NextBoundary = [this](int32 CellC, int32 Step)
    {
        return (static_cast<float>(CellC) + (Step > 0 ? 1.f : 0.f)) * static_cast<float>(LogicalCellSize);
    };
    float tMaxX = (StepX != 0) ? (NextBoundary(Cell.X, StepX) - Origin.X) / Dir.X : TNumericLimits<float>::Max();
    float tMaxY = (StepY != 0) ? (NextBoundary(Cell.Y, StepY) - Origin.Y) / Dir.Y : TNumericLimits<float>::Max();
    const float tDeltaX = (StepX != 0) ? LogicalCellSize / FMath::Abs(Dir.X) : TNumericLimits<float>::Max();
    const float tDeltaY = (StepY != 0) ? LogicalCellSize / FMath::Abs(Dir.Y) : TNumericLimits<float>::Max();

    TSet<FMassEntityHandle> Tested;   // 同一建筑跨多个粗格：只测一次
    FMassEntityHandle Best;
    const int32 MaxSteps = FMath::Clamp(FMath::CeilToInt(MaxDistance / LogicalCellSize) + 8, 64, 4096);

    for (int32 Step = 0; Step < MaxSteps; ++Step)
    {
        const FIntPoint Coarse = LogicalToCoarse(Cell);
        if (IsValidBuildingCoord(Coarse))
        {
            for (const FAsceGridBuildingPart& Part : BuildingCells[BuildingCoordToIndex(Coarse)].Buildings)
            {
                if (Tested.Contains(Part.Entity))
                {
                    continue;
                }
                Tested.Add(Part.Entity);

                // 命中盒 = footprint（旋转后 AABB）× HeightCm，**基座贴地**（`BaseZ` = 实体 Z）
                const int32 SizeX = FMath::Max(1, Part.Size.X);
                const int32 SizeY = FMath::Max(1, Part.Size.Y);
                const FVector2D Min2D(Part.Origin.X * (float)LogicalCellSize, Part.Origin.Y * (float)LogicalCellSize);
                const FVector2D Max2D((Part.Origin.X + SizeX) * (float)LogicalCellSize, (Part.Origin.Y + SizeY) * (float)LogicalCellSize);
                const FVector Center((Min2D.X + Max2D.X) * 0.5f, (Min2D.Y + Max2D.Y) * 0.5f, Part.BaseZ + Part.HeightCm * 0.5f);
                const FVector Extent((Max2D.X - Min2D.X) * 0.5f, (Max2D.Y - Min2D.Y) * 0.5f, Part.HeightCm * 0.5f);

                const float T = RayAABBHit(Origin, Dir, Center, Extent);
                if (T > 0.f && T < OutT)
                {
                    OutT = T;
                    Best = Part.Entity;
                }
            }
        }

        // 当前格出口时间已超过命中 t ⇒ 后面不可能更近
        const float NextT = FMath::Min(tMaxX, tMaxY);
        if (Best.IsValid() && NextT > OutT)
        {
            break;
        }
        if (NextT > MaxDistance)
        {
            break;
        }

        if (tMaxX < tMaxY)
        {
            Cell.X += StepX;
            tMaxX += tDeltaX;
        }
        else
        {
            Cell.Y += StepY;
            tMaxY += tDeltaY;
        }
        if (Cell.X < 0 || Cell.Y < 0 || Cell.X >= LogicalDim.X || Cell.Y >= LogicalDim.Y)
        {
            break;
        }
    }

    return Best;
}

UAsceSpatialGridSubsystem* UAsceSpatialGridSubsystem::Get(const UObject* WorldContextObject)
{
    UWorld* World = WorldContextObject->GetWorld();
    if (!IsValid(World))
    {
        return nullptr;
    }
    
    return World->GetSubsystem<UAsceSpatialGridSubsystem>();
}

float UAsceSpatialGridSubsystem::RayAABBHit(const FVector& Origin, const FVector& Dir, const FVector& BoxCenter, const FVector& BoxExtent) const
{
    FVector Min = BoxCenter - BoxExtent;
    FVector Max = BoxCenter + BoxExtent;

    float tNear = -BIG_NUMBER;
    float tFar  =  BIG_NUMBER;

    for (int i = 0; i < 3; ++i)
    {
        float invD = 1.f / Dir[i];
        float t_near = (Min[i] - Origin[i]) * invD;
        float t_far = (Max[i] - Origin[i]) * invD;

        if (t_near > t_far)
        {
            Swap(t_near, t_far);
        }

        tNear = FMath::Max(tNear, t_near);
        tFar  = FMath::Min(tFar,  t_far);

        if (tNear > tFar)
        {
            //没有交集， 不相交
            return -1;
        }
    }

    return (tNear >= 0) ? tNear : ((tFar >= 0) ? tFar : -1);
}

void UAsceSpatialGridSubsystem::DrawDebugFrustum(UWorld* World, const FConvexVolume& Frustum,
    const FVector NearCorners[4], const FVector FarCorners[4], const TArray<FVector2D>& GroundHull,
    const FVector& CameraLocation, float Duration)
{
    if (!World)
    {
        return;
    }

    // 1. 相机位置
    DrawDebugPoint(World, CameraLocation, 10.f, FColor::Yellow, false, Duration);

    // 2. 四根角射线 (相机 → 远平面角)
    for (int i = 0; i < 4; ++i)
    {
        DrawDebugLine(World, CameraLocation, FarCorners[i], FColor::Yellow, false, Duration, 0, 1.f);
    }

    // 3. 近平面矩形 (绿色)
    for (int i = 0; i < 4; ++i)
    {
        DrawDebugLine(World, NearCorners[i], NearCorners[(i + 1) % 4],
            FColor::Green, false, Duration, 0, 2.f);
    }

    // 4. 远平面矩形 (红色)
    for (int i = 0; i < 4; ++i)
    {
        DrawDebugLine(World, FarCorners[i], FarCorners[(i + 1) % 4],
            FColor::Red, false, Duration, 0, 2.f);
    }

    // 5. 近远平面连线 (灰色)
    for (int i = 0; i < 4; ++i)
    {
        DrawDebugLine(World, NearCorners[i], FarCorners[i],
            FColor(100, 100, 100), false, Duration, 0, 1.f);
    }

    // 6. 地面投影凸多边形 (蓝色)
    if (GroundHull.Num() >= 3)
    {
        for (int32 i = 0; i < GroundHull.Num(); ++i)
        {
            const FVector A(GroundHull[i].X, GroundHull[i].Y, 100.f);
            const FVector B(GroundHull[(i + 1) % GroundHull.Num()].X,
                           GroundHull[(i + 1) % GroundHull.Num()].Y, 100.f);
            DrawDebugLine(World, A, B, FColor::Cyan, false, Duration, 0, 3.f);
        }
    }
    
    const TCHAR* PlaneNames[6] = { TEXT("Near"), TEXT("Far"), TEXT("Left"), TEXT("Right"), TEXT("Top"), TEXT("Bottom") };
    const FColor PlaneColors[6] = {
        FColor::Green,   FColor::Red,
        FColor::Blue,    FColor::Magenta,
        FColor::Cyan,    FColor::Orange
    };
    
    // 每个面的参考点 (面中心或一角的顶点)
    const FVector NearCenter = (NearCorners[0] + NearCorners[1] + NearCorners[2] + NearCorners[3]) * 0.25f;
    const FVector FarCenter  = (FarCorners[0] + FarCorners[1] + FarCorners[2] + FarCorners[3]) * 0.25f;
    const FVector InsidePoint = (NearCenter + FarCenter) * 0.5f;
    // 每个面的中心点用于画线
    FVector PlaneCenters[6] = {
        NearCenter,
        FarCenter,
        (NearCorners[0] + FarCorners[0] + FarCorners[3] + NearCorners[3]) * 0.25f,  // Left face center
        (NearCorners[1] + NearCorners[2] + FarCorners[2] + FarCorners[1]) * 0.25f,  // Right face center
        (NearCorners[0] + NearCorners[1] + FarCorners[1] + FarCorners[0]) * 0.25f,  // Top face center
        (NearCorners[3] + FarCorners[3] + FarCorners[2] + NearCorners[2]) * 0.25f   // Bottom face center
    };
    
    for (int32 i = 0; i < Frustum.Planes.Num() && i < 6; ++i)
    {
        const FPlane& Plane = Frustum.Planes[i];
        FVector Normal = Plane.GetNormal();
        FVector Center = PlaneCenters[i];
        
        // 画法线 (从面中心向外 200 单位)
        DrawDebugLine(World, Center, Center + Normal * 200.f,
            PlaneColors[i], false, Duration, 0, 4.f);
        
        // 画箭头尖
        DrawDebugLine(World, Center + Normal * 190.f, Center + Normal * 170.f + Normal.Rotation().RotateVector(FVector(0, 15, 0)).GetSafeNormal() * 30.f,
            PlaneColors[i], false, Duration, 0, 4.f);
        
        // 标签
        DrawDebugString(World, Center + Normal * 220.f,
            FString::Printf(TEXT("%s dot=%.0f"), PlaneNames[i], Plane.PlaneDot(InsidePoint)),
            nullptr, PlaneColors[i], Duration, false, 1.f);
    }
}

void UAsceSpatialGridSubsystem::DrawDebugSelectionRay(UWorld* World, const FVector& Origin, const FVector& Direction,
    float MaxDistance, float Duration)
{
    if (!World) return;

    const FVector End = Origin + Direction * MaxDistance;

    // 射线
    DrawDebugLine(World, Origin, End, FColor::Orange, true, Duration, 0, 2.f);

    // 起点球
    DrawDebugSphere(World, Origin, 30.f, 8, FColor::Yellow, true, Duration);
}


void UAsceSpatialGridSubsystem::ConstructGrid(const FVector2D InWorldStart, const FVector2D InWorldEnd, const float InMinimalEntityRadius)
{
    WorldStart = FVector2D(FMath::Min(InWorldStart.X, InWorldEnd.X), FMath::Min(InWorldStart.Y, InWorldEnd.Y));
    WorldEnd   = FVector2D(FMath::Max(InWorldStart.X, InWorldEnd.X), FMath::Max(InWorldStart.Y, InWorldEnd.Y));
    MinimalEntityRadius = InMinimalEntityRadius;
    RefreshBaseGridConfig();

    const int32 TotalCells = GridDimension.X * GridDimension.Y;
    
    //处理网格实体数据
    EntitiesDataCells.Empty();
    EntitiesDataCells.AddDefaulted(TotalCells);

    UE_LOG(LogTemp, Log, TEXT("SpatialGrid: Constructed %d cells (%dx%d), cell size %.0f"), TotalCells, GridDimension.X, GridDimension.Y, GridCellSize);
}

// ──────────────────────────────────────────
// 建筑层
// ──────────────────────────────────────────

void UAsceSpatialGridSubsystem::ConstructBuildingsGrid(const FIntPoint LogicalGridDimension, const int32 InLogicalCellSize, const int32 InCoarseFactor)
{
	LogicalCellSize = FMath::Max(1, InLogicalCellSize);
	CoarseFactor = FMath::Max(1, InCoarseFactor);
	BuildingsGridDim = FIntPoint(
		FMath::Max(1, FMath::DivideAndRoundUp(FMath::Max(1, LogicalGridDimension.X), CoarseFactor)),
		FMath::Max(1, FMath::DivideAndRoundUp(FMath::Max(1, LogicalGridDimension.Y), CoarseFactor)));

	BuildingCells.Empty();
	BuildingCells.AddDefaulted(BuildingsGridDim.X * BuildingsGridDim.Y);

	UE_LOG(LogTemp, Log, TEXT("SpatialGrid: buildings grid %dx%d (coarse = %d logical cells, logical cell = %dcm)"),
		BuildingsGridDim.X, BuildingsGridDim.Y, CoarseFactor, LogicalCellSize);
}

const FAsceGridBuildingsData* UAsceSpatialGridSubsystem::GetBuildingsDataCell(const FIntPoint& CoarseCell) const
{
	return IsValidBuildingCoord(CoarseCell) ? &BuildingCells[BuildingCoordToIndex(CoarseCell)] : nullptr;
}

namespace
{
	/** 逻辑格中心是否落在建筑（**旋转后的最终 AABB**）footprint 内。 */
	FORCEINLINE bool IsLogicalCellInsidePart(const FAsceGridBuildingPart& Part, const FVector2D& CellCenter, int32 LogicalCellSize)
	{
		const FIntPoint Size(FMath::Max(1, Part.Size.X), FMath::Max(1, Part.Size.Y));
		return CellCenter.X >= Part.Origin.X * LogicalCellSize
			&& CellCenter.X < (Part.Origin.X + Size.X) * LogicalCellSize
			&& CellCenter.Y >= Part.Origin.Y * LogicalCellSize
			&& CellCenter.Y < (Part.Origin.Y + Size.Y) * LogicalCellSize;
	}

	/** AABB（中心 + 半长）是否与视锥相交（存在任一平面完全在外 ⇒ 不相交）。**单位与建筑共用**。 */
	FORCEINLINE bool IsAABBInsideFrustum(const FConvexVolume& Frustum, const FVector& Center, const FVector& Extent)
	{
		for (const FPlane& Plane : Frustum.Planes)
		{
			// AABB 在法线方向最靠内的角点
			const FVector Closest = Center - FVector(
				(Plane.X > 0 ? Extent.X : -Extent.X),
				(Plane.Y > 0 ? Extent.Y : -Extent.Y),
				(Plane.Z > 0 ? Extent.Z : -Extent.Z));
			if (Plane.PlaneDot(Closest) > 0.f)
			{
				return false;
			}
		}
		return true;
	}
}

void UAsceSpatialGridSubsystem::RebuildBuildingsData(const FAsceTickContext& Ctx)
{
	if (BuildingCells.Num() == 0 || !GridBuildingsUpdateProcessor)
	{
		return;
	}

	// 全清 + 由 processor **并行**重填（与单位层同一套：每格一把自旋锁）
	for (FAsceGridBuildingsData& Cell : BuildingCells)
	{
		Cell.Reset();
	}

	// 跑建筑 processor（并行重填）；MassEntity 子系统/实体管理器从上下文取
	if (Ctx.EntityManager)
	{
		FMassProcessingContext ProcessingContext(*Ctx.EntityManager, Ctx.DeltaTime);
		UE::Mass::Executor::Run(*GridBuildingsUpdateProcessor, ProcessingContext);
	}
}

void UAsceSpatialGridSubsystem::AddBuildingPart(const FAsceGridBuildingPart& Part)
{
	AddPartToCells(Part);
}

bool UAsceSpatialGridSubsystem::GetBuildingLogicalBounds(const FAsceGridBuildingPart& Part, FIntPoint& OutMin, FIntPoint& OutMax) const
{
	const FIntPoint Size(FMath::Max(1, Part.Size.X), FMath::Max(1, Part.Size.Y));
	OutMin = Part.Origin;
	OutMax = FIntPoint(Part.Origin.X + Size.X - 1, Part.Origin.Y + Size.Y - 1);
	return true;
}

void UAsceSpatialGridSubsystem::FindBuildingsInLogicalRect(const FIntPoint& Min, const FIntPoint& Max, TArray<FMassEntityHandle>& OutBuildings) const
{
	if (BuildingCells.Num() == 0)
	{
		return;
	}
	const FIntPoint Lo(FMath::Min(Min.X, Max.X), FMath::Min(Min.Y, Max.Y));
	const FIntPoint Hi(FMath::Max(Min.X, Max.X), FMath::Max(Min.Y, Max.Y));
	const FIntPoint CoarseMin = LogicalToCoarse(Lo);
	const FIntPoint CoarseMax = LogicalToCoarse(Hi);

	for (int32 cy = CoarseMin.Y; cy <= CoarseMax.Y; ++cy)
	{
		for (int32 cx = CoarseMin.X; cx <= CoarseMax.X; ++cx)
		{
			const FIntPoint Coord(cx, cy);
			if (!IsValidBuildingCoord(Coord))
			{
				continue;
			}
			for (const FAsceGridBuildingPart& Part : BuildingCells[BuildingCoordToIndex(Coord)].Buildings)
			{
				FIntPoint PMin, PMax;
				GetBuildingLogicalBounds(Part, PMin, PMax);
				if (PMin.X <= Hi.X && Lo.X <= PMax.X && PMin.Y <= Hi.Y && Lo.Y <= PMax.Y)
				{
					OutBuildings.AddUnique(Part.Entity);
				}
			}
		}
	}
}

void UAsceSpatialGridSubsystem::FindBuildingsInFrustum(const FConvexVolume& Frustum, const FVector2D& HullMin, const FVector2D& HullMax, TArray<FMassEntityHandle>& Out) const
{
	if (BuildingCells.Num() == 0 || LogicalCellSize <= 0)
	{
		return;
	}

	const FIntPoint LogicalDim = GetBuildingsLogicalDim();
	const FIntPoint LoCell(
		FMath::Clamp(FMath::FloorToInt(HullMin.X / LogicalCellSize), 0, LogicalDim.X - 1),
		FMath::Clamp(FMath::FloorToInt(HullMin.Y / LogicalCellSize), 0, LogicalDim.Y - 1));
	const FIntPoint HiCell(
		FMath::Clamp(FMath::FloorToInt(HullMax.X / LogicalCellSize), 0, LogicalDim.X - 1),
		FMath::Clamp(FMath::FloorToInt(HullMax.Y / LogicalCellSize), 0, LogicalDim.Y - 1));

	const FIntPoint CoarseMin = LogicalToCoarse(LoCell);
	const FIntPoint CoarseMax = LogicalToCoarse(HiCell);
	TSet<FMassEntityHandle> Seen;

	for (int32 cy = CoarseMin.Y; cy <= CoarseMax.Y; ++cy)
	{
		for (int32 cx = CoarseMin.X; cx <= CoarseMax.X; ++cx)
		{
			const FIntPoint Coord(cx, cy);
			if (!IsValidBuildingCoord(Coord))
			{
				continue;
			}
			for (const FAsceGridBuildingPart& Part : BuildingCells[BuildingCoordToIndex(Coord)].Buildings)
			{
				if (Seen.Contains(Part.Entity))
				{
					continue;
				}
				Seen.Add(Part.Entity);

				const int32 SizeX = FMath::Max(1, Part.Size.X);
				const int32 SizeY = FMath::Max(1, Part.Size.Y);
				const FVector2D Min2D(Part.Origin.X * (float)LogicalCellSize, Part.Origin.Y * (float)LogicalCellSize);
				const FVector2D Max2D((Part.Origin.X + SizeX) * (float)LogicalCellSize, (Part.Origin.Y + SizeY) * (float)LogicalCellSize);
				if (Max2D.X < HullMin.X || Min2D.X > HullMax.X || Max2D.Y < HullMin.Y || Min2D.Y > HullMax.Y)
				{
					continue;
				}

				// 命中盒 = footprint（旋转后 AABB）× HeightCm，**基座贴地**（与点选 `FindBuildingHitOnRay` 同口径）
				const FVector Center((Min2D.X + Max2D.X) * 0.5f, (Min2D.Y + Max2D.Y) * 0.5f, Part.BaseZ + Part.HeightCm * 0.5f);
				const FVector Extent((Max2D.X - Min2D.X) * 0.5f, (Max2D.Y - Min2D.Y) * 0.5f, Part.HeightCm * 0.5f);
				if (IsAABBInsideFrustum(Frustum, Center, Extent))
				{
					Out.Add(Part.Entity);   // `Seen` 已保证每栋只测一次 ⇒ 不需要 AddUnique
				}
			}
		}
	}
}

void UAsceSpatialGridSubsystem::GetPartCoarseRange(const FAsceGridBuildingPart& Part, FIntPoint& OutMin, FIntPoint& OutMax) const
{
	// footprint 已是"旋转后的最终 AABB" ⇒ 直接换算粗格（与 costfield/视野同一口径）
	const FIntPoint Size(FMath::Max(1, Part.Size.X), FMath::Max(1, Part.Size.Y));
	OutMin = LogicalToCoarse(Part.Origin);
	OutMax = LogicalToCoarse(FIntPoint(Part.Origin.X + Size.X - 1, Part.Origin.Y + Size.Y - 1));
}

void UAsceSpatialGridSubsystem::AddPartToCells(const FAsceGridBuildingPart& Part)
{
	FIntPoint Min, Max;
	GetPartCoarseRange(Part, Min, Max);
	for (int32 cy = Min.Y; cy <= Max.Y; ++cy)
	{
		for (int32 cx = Min.X; cx <= Max.X; ++cx)
		{
			const FIntPoint Coord(cx, cy);
			if (!IsValidBuildingCoord(Coord))
			{
				continue;
			}
			// 直接追加：整层每帧先 Reset 再重填，且同一次调用对同一粗格只走一遍 ⇒ 不需要查重
			// （并行 chunk 可能同时写同一粗格；互斥在 AddBuilding 里的每格自旋锁）
			BuildingCells[BuildingCoordToIndex(Coord)].AddBuilding(Part);
		}
	}
}

bool UAsceSpatialGridSubsystem::FindBuildingOnRay(const FIntPoint& FromLogicalCell, uint8 Dir, int32 MaxLogicalCells,
	FMassEntityHandle& OutBuilding, FIntPoint& OutEnterCell) const
{
	OutBuilding = FMassEntityHandle();
	OutEnterCell = FIntPoint::ZeroValue;
	if (BuildingCells.Num() == 0 || MaxLogicalCells <= 0)
	{
		return false;
	}

	int32 dx = 0, dy = 0;
	// Dir 约定与 AsceEther::EDir 一致：0=Left / 1=Up / 2=Right / 3=Down（这里不引入对 Ether 的依赖）
	switch (Dir & 3)
	{
		case 0: dx = -1; break;
		case 1: dy = 1; break;
		case 2: dx = 1; break;
		case 3: dy = -1; break;
		default: return false;
	}

	const FIntPoint LogicalDim(BuildingsGridDim.X * CoarseFactor, BuildingsGridDim.Y * CoarseFactor);
	int32 lx = FromLogicalCell.X;
	int32 ly = FromLogicalCell.Y;

	for (int32 step = 0; step < MaxLogicalCells; )
	{
		lx += dx;
		ly += dy;
		++step;
		if (lx < 0 || ly < 0 || lx >= LogicalDim.X || ly >= LogicalDim.Y)
		{
			break;
		}

		const FIntPoint Coarse = LogicalToCoarse(FIntPoint(lx, ly));
		if (!IsValidBuildingCoord(Coarse))
		{
			break;
		}

		// 粗格里有建筑：逐个候选做"逻辑格中心是否在 footprint 内"的细判
		const TArray<FAsceGridBuildingPart, TInlineAllocator<4>>& List = BuildingCells[BuildingCoordToIndex(Coarse)].Buildings;
		if (List.Num() > 0)
		{
			const FVector2D CellCenter(
				(static_cast<float>(lx) + 0.5f) * LogicalCellSize,
				(static_cast<float>(ly) + 0.5f) * LogicalCellSize);
			for (const FAsceGridBuildingPart& Part : List)
			{
				if (IsLogicalCellInsidePart(Part, CellCenter, LogicalCellSize))
				{
					OutBuilding = Part.Entity;
					OutEnterCell = FIntPoint(lx, ly);
					return true;
				}
			}
		}

		// 本粗格为空：直接跳到该粗格沿方向的出口，省掉格内剩余逻辑格
		// **非空粗格不能跳**：同一粗格内别的逻辑格可能被建筑覆盖（跳过去会穿墙）
		if (List.Num() == 0)
		{
			const int32 CoarseMinX = Coarse.X * CoarseFactor;
			const int32 CoarseMinY = Coarse.Y * CoarseFactor;
			int32 Jump = INDEX_NONE;
			if (dx > 0)      { Jump = (CoarseMinX + CoarseFactor - 1) - lx; }
			else if (dx < 0) { Jump = lx - CoarseMinX; }
			if (dy > 0)      { Jump = (Jump == INDEX_NONE) ? ((CoarseMinY + CoarseFactor - 1) - ly) : FMath::Min(Jump, (CoarseMinY + CoarseFactor - 1) - ly); }
			else if (dy < 0) { Jump = (Jump == INDEX_NONE) ? (ly - CoarseMinY) : FMath::Min(Jump, ly - CoarseMinY); }
			if (Jump > 0)
			{
				lx += dx * Jump;
				ly += dy * Jump;
				step += Jump;
			}
		}
	}
	return false;
}

void UAsceSpatialGridSubsystem::FindBuildingsInRadius(const FVector2D& CenterWorldLocation, float Radius, TArray<FMassEntityHandle>& OutBuildings) const
{
	if (BuildingCells.Num() == 0)
	{
		return;
	}
	const FVector2D MinWorld = CenterWorldLocation - FVector2D(Radius, Radius);
	const FVector2D MaxWorld = CenterWorldLocation + FVector2D(Radius, Radius);
	const FIntPoint CoarseMin = LogicalToCoarse(FIntPoint(
		FMath::FloorToInt(MinWorld.X / LogicalCellSize), FMath::FloorToInt(MinWorld.Y / LogicalCellSize)));
	const FIntPoint CoarseMax = LogicalToCoarse(FIntPoint(
		FMath::FloorToInt(MaxWorld.X / LogicalCellSize), FMath::FloorToInt(MaxWorld.Y / LogicalCellSize)));

	const float RadiusSq = Radius * Radius;
	TSet<FMassEntityHandle> Seen;
	for (int32 cy = CoarseMin.Y; cy <= CoarseMax.Y; ++cy)
	{
		for (int32 cx = CoarseMin.X; cx <= CoarseMax.X; ++cx)
		{
			const FIntPoint Coord(cx, cy);
			if (!IsValidBuildingCoord(Coord))
			{
				continue;
			}
			for (const FAsceGridBuildingPart& Part : BuildingCells[BuildingCoordToIndex(Coord)].Buildings)
			{
				if (Seen.Contains(Part.Entity))
				{
					continue;
				}
				const FVector2D PartCenter(
					(static_cast<float>(Part.Origin.X) + Part.Size.X * 0.5f) * LogicalCellSize,
					(static_cast<float>(Part.Origin.Y) + Part.Size.Y * 0.5f) * LogicalCellSize);
				const FVector2D Half(
					Part.Size.X * 0.5f * LogicalCellSize,
					Part.Size.Y * 0.5f * LogicalCellSize);
				const FVector2D Closest(
					FMath::Clamp(CenterWorldLocation.X, PartCenter.X - Half.X, PartCenter.X + Half.X),
					FMath::Clamp(CenterWorldLocation.Y, PartCenter.Y - Half.Y, PartCenter.Y + Half.Y));
				if (FVector2D::DistSquared(Closest, CenterWorldLocation) <= RadiusSq)
				{
					Seen.Add(Part.Entity);
					OutBuildings.Add(Part.Entity);
				}
			}
		}
	}
}

const FAsceSpatialGridEntitiesData* UAsceSpatialGridSubsystem::GetGridEntitiesDataCell(FIntVector2 GridLocation)
{
    FIntVector Coord = FIntVector(GridLocation.X, GridLocation.Y, 0);
    if (IsValidCoord(Coord))
    {
        return &EntitiesDataCells[CoordToIndex(Coord)];
    }
    return nullptr;
}


// ── InsertEntity：只锁目标格子
void UAsceSpatialGridSubsystem::InsertEntity(FMassEntityHandle EntityHandle, const FVector& Location, float Radius, float Height)
{
    const FIntVector CenterCoord = LocationToCoord(Location);
    const FAsceGridEntityData Data = { EntityHandle, Location, Radius, Height };

    if (Radius <= GridCellSize * 0.5f)
    {
        if (IsValidCoord(CenterCoord))
        {
            EntitiesDataCells[CoordToIndex(CenterCoord)].AddEntity(Data);
        }
        return;
    }

    // 大单位：多格写入，每格单独加锁
    const FVector HalfExtent(Radius, Radius, 0.f);
    const FIntVector MinCoord = LocationToCoord(Location - HalfExtent);
    const FIntVector MaxCoord = LocationToCoord(Location + HalfExtent);

    for (int32 y = MinCoord.Y; y <= MaxCoord.Y; ++y)
    {
        for (int32 x = MinCoord.X; x <= MaxCoord.X; ++x)
        {
            const FIntVector Coord(x, y, 0);
            if (IsValidCoord(Coord))
            {
                EntitiesDataCells[CoordToIndex(Coord)].AddEntity(Data);
            }
        }
    }
}

// ── 查询 ──────────────────────────────────────────

void UAsceSpatialGridSubsystem::FindEntities_Internal(const FVector2D& StartWorldLocation, const FVector2D& EndWorldLocation, TArray<FMassEntityHandle>& OutHandles)
{
    if (EntitiesDataCells.Num() == 0)
    {
        return;
    }

    // 2D 矩形 → 格子范围
    const float MinX = FMath::Min(StartWorldLocation.X, EndWorldLocation.X);
    const float MinY = FMath::Min(StartWorldLocation.Y, EndWorldLocation.Y);
    const float MaxX = FMath::Max(StartWorldLocation.X, EndWorldLocation.X);
    const float MaxY = FMath::Max(StartWorldLocation.Y, EndWorldLocation.Y);

    const FIntVector MinCoord = LocationToCoord(FVector(MinX, MinY, 0.f));
    const FIntVector MaxCoord = LocationToCoord(FVector(MaxX, MaxY, 0.f));

    // 去重用（框选可能跨越几十个格子）
    TSet<FMassEntityHandle> HandleSet;

    for (int32 y = MinCoord.Y; y <= MaxCoord.Y; ++y)
    {
        for (int32 x = MinCoord.X; x <= MaxCoord.X; ++x)
        {
            const FIntVector Coord(x, y, 0);
            if (!IsValidCoord(Coord))
            {
                continue;
            }

            const int32 CellIndex = x + GridDimension.X * y;
            for (const auto& Entity : EntitiesDataCells[CellIndex].Entities)
            {
                // 精确过滤：实体位置必须在矩形内
                if (Entity.CacheEntityLocation.X + Entity.CachedEntityRadius >= MinX &&
                    Entity.CacheEntityLocation.X - Entity.CachedEntityRadius <= MaxX &&
                    Entity.CacheEntityLocation.Y + Entity.CachedEntityRadius >= MinY &&
                    Entity.CacheEntityLocation.Y - Entity.CachedEntityRadius <= MaxY)
                {
                    HandleSet.Add(Entity.EntityHandle);
                }
            }
        }
    }

    OutHandles = HandleSet.Array();
}

void UAsceSpatialGridSubsystem::FindEntities_Internal(const FVector2D& CenterLocation, const float Radius, TArray<FMassEntityHandle>& OutHandles)
{
    if (EntitiesDataCells.Num() == 0)
    {
        return;
    }

    // 圆圈 -> 外接2D 矩形 → 格子范围
    const float MinX = CenterLocation.X - Radius;
    const float MinY = CenterLocation.Y - Radius;
    const float MaxX = CenterLocation.X + Radius;
    const float MaxY = CenterLocation.Y + Radius;

    const FIntVector MinCoord = LocationToCoord(FVector(MinX, MinY, 0.f));
    const FIntVector MaxCoord = LocationToCoord(FVector(MaxX, MaxY, 0.f));
    
    // 去重用（框选可能跨越几十个格子）
    TSet<FMassEntityHandle> HandleSet;

    for (int32 y = MinCoord.Y; y <= MaxCoord.Y; ++y)
    {
        for (int32 x = MinCoord.X; x <= MaxCoord.X; ++x)
        {
            const FIntVector Coord(x, y, 0);
            if (!IsValidCoord(Coord))
            {
                continue;
            }

            const int32 CellIndex = x + GridDimension.X * y;
            for (const auto& Entity : EntitiesDataCells[CellIndex].Entities)
            {
                // 精确过滤：实体位置必须在矩形内
                if (UKismetMathLibrary::DistanceSquared2D(FVector2D(Entity.CacheEntityLocation.X, Entity.CacheEntityLocation.Y), CenterLocation) < Radius * Radius)
                {
                    HandleSet.Add(Entity.EntityHandle);
                }
            }
        }
    }
    OutHandles = HandleSet.Array();
}

void UAsceSpatialGridSubsystem::GetScreenCornerRays(const APlayerController* Controller, const FVector2D& MinScreen, const FVector2D& MaxScreen, FVector OutOrigins[4], FVector OutDirs[4])
{
    // 屏幕空间角点顺序: TopLeft, TopRight, BottomRight, BottomLeft
    const FVector2D Corners[4] = {
        FVector2D(MinScreen.X, MinScreen.Y),
        FVector2D(MaxScreen.X, MinScreen.Y),
        FVector2D(MaxScreen.X, MaxScreen.Y),
        FVector2D(MinScreen.X, MaxScreen.Y)
    };

    for (int32 i = 0; i < 4; ++i)
    {
        Controller->DeprojectScreenPositionToWorld(Corners[i].X, Corners[i].Y,OutOrigins[i], OutDirs[i]);
    }
}

FConvexVolume UAsceSpatialGridSubsystem::BuildFrustumFromCorners(const FVector NearCorners[4], const FVector FarCorners[4])
{
    TArray<FPlane> Planes;
    Planes.Reserve(6);

    const FVector NearCenter = (NearCorners[0] + NearCorners[1] + NearCorners[2] + NearCorners[3]) * 0.25f;
    const FVector FarCenter  = (FarCorners[0] + FarCorners[1] + FarCorners[2] + FarCorners[3]) * 0.25f;
    const FVector InsidePt   = (NearCenter + FarCenter) * 0.5f;

    // 确保法线指向外侧: InsidePt 的 PlaneDot <= 0
    auto MakeOutward = [&](const FVector& PlanePt, FVector Normal) -> FPlane
    {
        Normal.Normalize();
        if (FVector::DotProduct(Normal, InsidePt - PlanePt) > 0.f)
            Normal = -Normal;
        return FPlane(PlanePt, Normal);
    };

    // 四条边向量 (近→远)
    const FVector Edge0 = FarCorners[0] - NearCorners[0];
    const FVector Edge1 = FarCorners[1] - NearCorners[1];
    const FVector Edge2 = FarCorners[2] - NearCorners[2];
    const FVector Edge3 = FarCorners[3] - NearCorners[3];

    // Near: 法线指向相机 (NearCenter → 相机方向 ≈ 远→近)
    Planes.Add(MakeOutward(NearCenter, NearCenter - FarCenter));

    // Far: 法线指向地下 (Z=0 以下为外)
    Planes.Add(MakeOutward(FarCorners[0], FVector::UpVector));

    // 左面 (Edge0 × Edge3)
    Planes.Add(MakeOutward(NearCorners[0],
        FVector::CrossProduct(Edge0, Edge3)));

    // 右面 (Edge1 × Edge2)
    Planes.Add(MakeOutward(NearCorners[1],
        FVector::CrossProduct(Edge1, Edge2)));

    // 上面 (Edge0 × Edge1)
    Planes.Add(MakeOutward(NearCorners[0],
        FVector::CrossProduct(Edge0, Edge1)));

    // 下面 (Edge3 × Edge2)
    Planes.Add(MakeOutward(NearCorners[3],
        FVector::CrossProduct(Edge3, Edge2)));

    FConvexVolume Volume;
    Volume.Planes = MoveTemp(Planes);
    return Volume;
}

TArray<FVector2D> UAsceSpatialGridSubsystem::ProjectFrustumToGround(const FVector NearCorners[4], const FVector FarCorners[4])
{
    TArray<FVector2D> Points;
    Points.Reserve(8);

    for (int32 i = 0; i < 4; ++i)
    {
        Points.Add(FVector2D(NearCorners[i].X, NearCorners[i].Y));
        Points.Add(FVector2D(FarCorners[i].X, FarCorners[i].Y));
    }

    return ComputeConvexHull2D(Points);
}

TArray<FVector2D> UAsceSpatialGridSubsystem::ComputeConvexHull2D(const TArray<FVector2D>& Points)
{
    //Andrew's Monotone Chain 凸包算法
    if (Points.Num() <= 1)
    {
        return Points;
    }

    TArray<FVector2D> Sorted = Points;
    Sorted.Sort([](const FVector2D& A, const FVector2D& B) {
        if (A.X != B.X)
        {
            return A.X < B.X;
        }
        return A.Y < B.Y;
    });

    TArray<FVector2D> Hull;

    // 叉积: (B-A) × (C-B) = (B.X-A.X)*(C.Y-B.Y) - (B.Y-A.Y)*(C.X-B.X)
    // 此处几何意义为Cross > 0 时，BC相对于AB向左转，Cross < 0时，BC相对于AB向右转
    auto Cross = [](const FVector2D& A, const FVector2D& B, const FVector2D& C) -> float {
        return (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
    };

    // Lower hull，先计算下方的凸包
    for (const FVector2D& P : Sorted)
    {
        while (Hull.Num() >= 2)
        {
            const int32 N = Hull.Num();
            if (Cross(Hull[N - 2], Hull[N - 1], P) > 0.f)
            {
                break;
            }
            Hull.Pop();
        }
        Hull.Add(P);
    }

    // Upper hull，计算上方的凸包
    TArray<FVector2D> Upper;
    for (int32 i = Sorted.Num() - 1; i >= 0; --i)
    {
        const FVector2D& P = Sorted[i];
        while (Upper.Num() >= 2)
        {
            const int32 N = Upper.Num();
            if (Cross(Upper[N - 2], Upper[N - 1], P) > 0.f)
            {
                break;
            }
            Upper.Pop();
        }
        Upper.Add(P);
    }

    // 合并 (去掉重复的首尾点)
    Hull.Pop();        // 去掉 lower 的最后一个 = upper 的第一个
    Upper.Pop();       // 去掉 upper 的最后一个 = lower 的第一个
    Hull.Append(Upper);

    return Hull;
}

void UAsceSpatialGridSubsystem::GetConvexHullAABB2D(const TArray<FVector2D>& Hull, FVector2D& OutMin, FVector2D& OutMax)
{
    if (Hull.Num() == 0)
    {
        OutMin = OutMax = FVector2D::ZeroVector;
        return;
    }

    OutMin = OutMax = Hull[0];
    for (int32 i = 1; i < Hull.Num(); ++i)
    {
        OutMin.X = FMath::Min(OutMin.X, Hull[i].X);
        OutMin.Y = FMath::Min(OutMin.Y, Hull[i].Y);
        OutMax.X = FMath::Max(OutMax.X, Hull[i].X);
        OutMax.Y = FMath::Max(OutMax.Y, Hull[i].Y);
    }
}

void UAsceSpatialGridSubsystem::FindEntitiesInFrustum(const FConvexVolume& Frustum, const FVector2D& AABBMin, const FVector2D& AABBMax, TArray<FMassEntityHandle>& OutHandles) const
{
    // **契约：追加**（不清空输出；和 `FindBuildingsInFrustum/InRadius/InLogicalRect` 一致）。
    // 调用方要"本次查询结果"就自己在调用前 Reset（框选入口就是这么做的）。
    if (EntitiesDataCells.Num() == 0)
    {
        return;
    }

    const FIntVector MinCoord = LocationToCoord(FVector(AABBMin.X, AABBMin.Y, 0.f));
    const FIntVector MaxCoord = LocationToCoord(FVector(AABBMax.X, AABBMax.Y, 0.f));
    TSet<FMassEntityHandle> HandleSet;

    for (int32 y = MinCoord.Y; y <= MaxCoord.Y; ++y)
    {
        for (int32 x = MinCoord.X; x <= MaxCoord.X; ++x)
        {
            const FIntVector Coord(x, y, 0);
            if (!IsValidCoord(Coord))
            {
                continue;
            }

            const int32 CellIndex = CoordToIndex(Coord);
            for (const FAsceGridEntityData& Entity : EntitiesDataCells[CellIndex].Entities)
            {
                // 用网格内缓存的位置 + 半径 + 默认半高构造 3D AABB
                const FVector Center(
                    Entity.CacheEntityLocation.X,
                    Entity.CacheEntityLocation.Y,
                    Entity.CacheEntityLocation.Z + Entity.CachedEntityHeight / 2);

                const FVector Extent(
                    Entity.CachedEntityRadius,
                    Entity.CachedEntityRadius,
                    Entity.CachedEntityHeight / 2);

                if (IsAABBInsideFrustum(Frustum, Center, Extent))
                {
                    HandleSet.Add(Entity.EntityHandle);
                }
            }
        }
    }

    // 追加（本次查询内部按 handle 去重）
    OutHandles.Append(HandleSet.Array());
}



