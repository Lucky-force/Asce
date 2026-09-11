// AsceMapBaker.cpp
#if WITH_EDITOR
#include "AsceMapBaker.h"

#include "AsceBakedMapData.h"
#include "AsceMapDims.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"

AAsceMapBaker::AAsceMapBaker()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AAsceMapBaker::Bake()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!OutputData)
	{
		UE_LOG(LogTemp, Error, TEXT("[AsceMapBaker] 未指定 OutputData（UAsceBakedMapData）"));
		return;
	}

	int32 SectorX = SectorCountX;
	int32 SectorY = SectorCountY;
	int32 FlowW = 0, FlowH = 0, VisW = 0, VisH = 0;
	float CellCm = 50.f;
	AsceMapDims::Compute(SectorX, SectorY, VisionDetailLevel, FlowW, FlowH, VisW, VisH, CellCm);

	// 收集阻挡 Actor 及其 XY 包围盒
	TArray<AActor*> Blockers;
	TArray<FBox2D> BlockerBoxes;
	if (BlockerClass)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			if (!A || !A->IsA(BlockerClass))
			{
				continue;
			}
			const FBox B = A->GetComponentsBoundingBox(/*bNonColliding=*/true);
			if (!B.IsValid)
			{
				continue;
			}
			Blockers.Add(A);
			BlockerBoxes.Add(FBox2D(FVector2D(B.Min.X, B.Min.Y), FVector2D(B.Max.X, B.Max.Y)));
		}
	}

	FCollisionQueryParams Params(FName(TEXT("AsceMapBake")), /*bTraceComplex=*/false);
	Params.AddIgnoredActors(Blockers);

	// —— 高度（视野格）——
	TArray<uint16> Heights;
	Heights.SetNumZeroed(VisW * VisH);
	for (int32 Y = 0; Y < VisH; ++Y)
	{
		for (int32 X = 0; X < VisW; ++X)
		{
			const float CX = WorldOrigin.X + (static_cast<float>(X) + 0.5f) * CellCm;
			const float CY = WorldOrigin.Y + (static_cast<float>(Y) + 0.5f) * CellCm;
			const FVector Start(CX, CY, TraceMaxZCm);
			const FVector End(CX, CY, 0.f);

			FHitResult Hit;
			uint16 Height = DefaultHeightCm;
			if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
			{
				Height = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(Hit.ImpactPoint.Z), 0, 65535));
			}
			Heights[Y * VisW + X] = Height;
		}
	}

	// —— 阻挡（流场格，XY AABB 相交）——
	TArray<uint8> Blocked;
	Blocked.SetNumZeroed(FlowW * FlowH);
	for (int32 Y = 0; Y < FlowH; ++Y)
	{
		for (int32 X = 0; X < FlowW; ++X)
		{
			const float MinX = WorldOrigin.X + static_cast<float>(X) * GRID_SIZE;
			const float MinY = WorldOrigin.Y + static_cast<float>(Y) * GRID_SIZE;
			const float MaxX = MinX + GRID_SIZE;
			const float MaxY = MinY + GRID_SIZE;

			bool bBlocked = false;
			for (const FBox2D& Box : BlockerBoxes)
			{
				if (Box.bIsValid && MinX < Box.Max.X && MaxX > Box.Min.X && MinY < Box.Max.Y && MaxY > Box.Min.Y)
				{
					bBlocked = true;
					break;
				}
			}
			Blocked[Y * FlowW + X] = bBlocked ? 1 : 0;
		}
	}

	// —— 写入 DA ——
	OutputData->Modify();
	OutputData->SectorCountX = SectorX;
	OutputData->SectorCountY = SectorY;
	OutputData->VisionCellSizeCm = CellCm;
	OutputData->FlowGridW = FlowW;
	OutputData->FlowGridH = FlowH;
	OutputData->VisionGridW = VisW;
	OutputData->VisionGridH = VisH;
	OutputData->WorldOrigin = WorldOrigin;
	OutputData->StaticHeights = MoveTemp(Heights);
	OutputData->Blocked = MoveTemp(Blocked);
	OutputData->MarkPackageDirty();

	UE_LOG(LogTemp, Log, TEXT("[AsceMapBaker] 烘焙完成：流场格 %dx%d，视野格 %dx%d，阻挡Actor=%d"),
		FlowW, FlowH, VisW, VisH, Blockers.Num());
}
#endif // WITH_EDITOR
