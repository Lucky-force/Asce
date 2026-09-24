// Fill out your copyright notice in the Description page of Project Settings.


#include "AsceSpatialGridDebugger.h"

#include "AsceSpatialGridSubsystem.h"
#include "Components/DecalComponent.h"
#include "Kismet/KismetStringLibrary.h"

AAsceSpatialGridDebugger::AAsceSpatialGridDebugger(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>("Root");
	DebugDecal = CreateDefaultSubobject<UDecalComponent>("DecalComponent");
    DebugDecal->SetupAttachment(RootComponent);
}

void AAsceSpatialGridDebugger::BeginPlay()
{
    Super::BeginPlay();
    
#if WITH_EDITOR
    UAsceSpatialGridSubsystem* SpatialGridSubsystem = UAsceSpatialGridSubsystem::Get(this);
    if (IsValid(SpatialGridSubsystem))
    {
        SpatialGridSubsystem->bPaintDebugInfo = bDrawDebugGrid;
        SpatialGridSubsystem->DrawTime = DrawTime;
    }
#endif
}

void AAsceSpatialGridDebugger::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (bDrawDebugGrid)
    { 
        DebugRefreshTimer -= DeltaTime;
        if (DebugRefreshTimer <= 0.f)
        {
            DebugRefreshTimer = DebugRefreshInterval;
            DrawDebugGrid();
            DrawDebugEntityBounds();
        }
    }
    else
    {
        DebugRefreshTimer = 0.f;  // 关闭时归零，下回开立即刷新
    }
}

void AAsceSpatialGridDebugger::DrawDebugGrid()
{
    UAsceSpatialGridSubsystem* SpatialGridSubsystem = UAsceSpatialGridSubsystem::Get(this);
    if (!IsValid(SpatialGridSubsystem) || SpatialGridSubsystem->GridDimension.X == 0)
    {
        return;
    } 
    const int32 Width = SpatialGridSubsystem->GridDimension.X;
    const int32 Height = SpatialGridSubsystem->GridDimension.Y;
    const int32 TexWidth  = Width  * CellPixelSize;
    const int32 TexHeight = Height * CellPixelSize;
    // 1. 创建/复用 transient texture
    if (!IsValid(DebugTexture) || DebugTexture->GetSizeX() != TexWidth || DebugTexture->GetSizeY() != TexHeight)
    {
        DebugTexture = UTexture2D::CreateTransient(TexWidth, TexHeight, PF_B8G8R8A8);
    }
    
    if (!IsValid(DebugTexture))
    {
        return;
    }
    
    FTexture2DMipMap& Mip = DebugTexture->GetPlatformData()->Mips[0];
    uint8* Pixels = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    
    // 2. 写像素: 每个 cell 4 字节 BGRA
    for (int32 y = 0; y < Height; ++y)
    {
        for (int32 x = 0; x < Width; ++x)
        {
            if (const FAsceSpatialGridEntitiesData* EntitiesData = SpatialGridSubsystem->GetGridEntitiesDataCell(FIntVector2(x, y)))
            {
                const int32 Count = EntitiesData->GetNumEntities();
                const uint8 R = static_cast<uint8>(FMath::Clamp(Count * 15, 0, 255));
                const uint8 G = (Count == 0) ? 50 : 0;
                const uint8 B = 0;
                const uint8 A = (Count == 0) ? 200 : 255;

                for (int32 py = 0; py < CellPixelSize; ++py)
                {
                    for (int32 px = 0; px < CellPixelSize; ++px)
                    {
                        const int32 PixelIdx = ((y * CellPixelSize + py) * TexWidth + (x * CellPixelSize + px)) * 4;
                
                        // 块的最后一列/行 → 黑色分隔线
                        const bool bEdge = (px == CellPixelSize - 1 || py == CellPixelSize - 1);

                        Pixels[PixelIdx + 0] = bEdge ? 0 : B;
                        Pixels[PixelIdx + 1] = bEdge ? 0 : G;
                        Pixels[PixelIdx + 2] = bEdge ? 0 : R;
                        Pixels[PixelIdx + 3] = bEdge ? 255 : A;
                    }
                }
            }
            else
            {
                for (int32 py = 0; py < CellPixelSize; ++py)
                {
                    for (int32 px = 0; px < CellPixelSize; ++px)
                    {
                        const int32 PixelIdx = ((y * CellPixelSize + py) * TexWidth + (x * CellPixelSize + px)) * 4;
                        
                        // 无效 cell (黑色)
                        Pixels[PixelIdx + 0] = 0;
                        Pixels[PixelIdx + 1] = 0;
                        Pixels[PixelIdx + 2] = 0;
                        Pixels[PixelIdx + 3] = 0;
                    }
                }
            }
        }
    }

    Mip.BulkData.Unlock();
    DebugTexture->UpdateResource();

    // 3. 首次创建 decal
    if (!IsValid(DebugDecal))
    {
        return;
    }
    
    float GridSize = SpatialGridSubsystem->GridCellSize;
    // Decal 尺寸 = 网格世界范围

    DebugDecal->DecalSize = FVector(100.f, Height * GridSize * 0.5f, Width * GridSize * 0.5f);
    DebugDecal->SetWorldLocation(FVector(
    SpatialGridSubsystem->WorldStart.X + Width * GridSize * 0.5f,
    SpatialGridSubsystem->WorldStart.Y + Height * GridSize * 0.5f,
    DebugDrawHeight));

    // 材质需要能从纹理读颜色直出 (Emissive/unlit)
    if (!DebugDecal->GetMaterial(0) && IsValid(DebugDecalMaterial))
    {
        DebugDecal->SetMaterial(0, DebugDecalMaterial);
    }

    if (!IsValid(DebugMID) && IsValid(DebugDecal->GetMaterial(0)))                 
    {
        DebugMID = DebugDecal->CreateDynamicMaterialInstance();
    }
    
    // 每帧更新纹理（复用缓存的 MID）
    if (IsValid(DebugMID))
    {
        DebugMID->SetTextureParameterValue(TEXT("DebugTexture"), DebugTexture);
    }
    DebugDecal->SetVisibility(bDrawDebugGrid);
}


void AAsceSpatialGridDebugger::DrawDebugEntityBounds()
{
    UAsceSpatialGridSubsystem* Grid = UAsceSpatialGridSubsystem::Get(this);
    if (!IsValid(Grid))
    {
        return;
    }
    
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return;
    }
    
    APlayerController* LocalPlayerController = World->GetFirstPlayerController();
    
    if (!IsValid(LocalPlayerController))
    {
        return;
    }
    
    APawn* PlayerPawn = LocalPlayerController->GetPawn();
    
    if (!IsValid(PlayerPawn))
    {
        return;
    }

    FIntVector Coord = Grid->LocationToCoord(PlayerPawn->GetActorLocation());
    int StartX = Coord.X - EntityDebugRange;
    int EndX = Coord.X + EntityDebugRange;
    int StartY = Coord.Y - EntityDebugRange;
    int EndY = Coord.Y + EntityDebugRange;
    
    if (StartX < 0)
    {
        StartX = 0;
    }
    else if (StartX >= Grid->GridDimension.X)
    {
        return;
    }
    
    if (EndX < 0)
    {
        return;
    }
    else if (EndX > Grid->GridDimension.X)
    {
        EndX = Grid->GridDimension.X;
    }
    
    if (StartY < 0)
    {
        StartY = 0;
    }
    else if (StartY >= Grid->GridDimension.Y)
    {
        return;
    }
    
    if (EndY < 0)
    {
        return;
    }
    else if (EndY > Grid->GridDimension.Y)
    {
        EndY = Grid->GridDimension.Y;
    }
    
    DrawnEntities.Reset();
    for (int32 x = StartX; x < EndX; ++x)
    {
        for (int32 y = StartY; y < EndY; ++y)
        {
            const auto* CellData = Grid->GetGridEntitiesDataCell(FIntVector2(x, y));
            if (!CellData)
            {
                continue;
            }

            CellData->ForEachEntity([&](const FAsceGridEntityData& E)
            {
                if (DrawnEntities.Contains(E.EntityHandle))
                {
                    return;
                }
                DrawDebugBox(GetWorld(),FVector(E.CacheEntityLocation.X, E.CacheEntityLocation.Y, E.CacheEntityLocation.Z + E.CachedEntityHeight * 0.5f), FVector(E.CachedEntityRadius, E.CachedEntityRadius, E.CachedEntityHeight * 0.5f),
                    FColor::Cyan,
                    false,  // ← persistent
                    DebugRefreshInterval,  // ← 留到下次刷新
                    0, 0.5f
                    );
                DrawnEntities.Add(E.EntityHandle);
            });
        }
    }
}