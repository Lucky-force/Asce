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

	// bTraceComplex=true：Landscape 的精确高度只在复杂碰撞里（简单碰撞对 heightfield 粗糙/不完整）
	FCollisionQueryParams Params(FName(TEXT("AsceMapBake")), /*bTraceComplex=*/true);
	Params.AddIgnoredActors(Blockers);

	// —— 高度（视野格）——
	TArray<uint16> Heights;
	Heights.SetNumZeroed(VisW * VisH);
	int32 HeightHitCount = 0;
	for (int32 Y = 0; Y < VisH; ++Y)
	{
		for (int32 X = 0; X < VisW; ++X)
		{
			const float CX = (static_cast<float>(X) + 0.5f) * CellCm;
			const float CY = (static_cast<float>(Y) + 0.5f) * CellCm;
			const FVector Start(CX, CY, TraceMaxZCm);
			const FVector End(CX, CY, 0.f);

			FHitResult Hit;
			uint16 Height = DefaultHeightCm;
			if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
			{
				Height = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(Hit.ImpactPoint.Z), 0, 65535));
				++HeightHitCount;
			}
			Heights[Y * VisW + X] = Height;
		}
	}

	// —— 阻挡（流场格）——
	// 判定方式：逐格竖直射线（从 TraceMaxZCm 向下打到 0，简单碰撞），命中任一 BlockerActor → 阻挡。
	// Blocker 的 XY AABB 只用来粗筛候选格（只对与 AABB 相交的格子打射线，避免全图上百万格全打）——
	// 实际判定由射线碰撞决定：旋转/异形物体不会像"直接把 AABB 填格子"那样多标一大片。
	TArray<uint8> Blocked;
	Blocked.SetNumZeroed(FlowW * FlowH);
	int32 BlockHitCount = 0;
	if (Blockers.Num() > 0)
	{
		TSet<AActor*> BlockerSet;
		BlockerSet.Reserve(Blockers.Num());
		BlockerSet.Append(Blockers);

		// 阻挡判定用简单碰撞（和玩法单位的世界碰撞一致）；复杂碰撞只给上面的地形高度采样用。
		FCollisionQueryParams BlockParams(FName(TEXT("AsceMapBakeBlock")), /*bTraceComplex=*/false);

		// 候选格范围 = 所有 blocker AABB 的并集（clamp 到图内）
		FBox2D Union(ForceInit);
		for (const FBox2D& Box : BlockerBoxes)
		{
			if (Box.bIsValid)
			{
				Union += Box;
			}
		}
		if (Union.bIsValid)
		{
			const int32 X0 = FMath::Clamp(FMath::FloorToInt(Union.Min.X / GRID_SIZE), 0, FlowW - 1);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt(Union.Min.Y / GRID_SIZE), 0, FlowH - 1);
			const int32 X1 = FMath::Clamp(FMath::CeilToInt(Union.Max.X / GRID_SIZE) - 1, 0, FlowW - 1);
			const int32 Y1 = FMath::Clamp(FMath::CeilToInt(Union.Max.Y / GRID_SIZE) - 1, 0, FlowH - 1);

			// 每格采样：中心 + 4 角。只打中心时，贴着格边界的薄墙会被两格中心"夹缝漏掉"；
			// 角点射线（与相邻格共享）能兜住这种情况。任一采样点命中 BlockerActor 即置位。
			static const float SampleOffset[5][2] = {
				{ 0.5f, 0.5f }, { 0.f, 0.f }, { 1.f, 0.f }, { 0.f, 1.f }, { 1.f, 1.f }
			};

			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					bool bBlockedCell = false;
					for (int32 S = 0; S < 5 && !bBlockedCell; ++S)
					{
						const float CX = (static_cast<float>(X) + SampleOffset[S][0]) * GRID_SIZE;
						const float CY = (static_cast<float>(Y) + SampleOffset[S][1]) * GRID_SIZE;

						FHitResult Hit;
						if (World->LineTraceSingleByChannel(Hit, FVector(CX, CY, TraceMaxZCm), FVector(CX, CY, 0.f), ECC_Visibility, BlockParams))
						{
							if (BlockerSet.Contains(Hit.GetActor()))
							{
								bBlockedCell = true;
							}
						}
					}
					if (bBlockedCell)
					{
						Blocked[Y * FlowW + X] = 1;
						++BlockHitCount;
					}
				}
			}
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
	OutputData->StaticHeights = MoveTemp(Heights);
	OutputData->Blocked = MoveTemp(Blocked);
	OutputData->MarkPackageDirty();

	UE_LOG(LogTemp, Log, TEXT("[AsceMapBaker] 烘焙完成：流场格 %dx%d，视野格 %dx%d，阻挡Actor=%d，阻挡命中格 %d，高度射线命中 %d / %d"),
		FlowW, FlowH, VisW, VisH, Blockers.Num(), BlockHitCount, HeightHitCount, VisW * VisH);
}
#endif // WITH_EDITOR
