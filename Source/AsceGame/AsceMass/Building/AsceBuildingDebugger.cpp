// AsceBuildingDebugger.cpp
#include "AsceBuildingDebugger.h"

#include "AsceBuildingSubsystem.h"
#include "AsceMass/Building/AsceBuildingDef.h"
#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "AsceMass/SpatialGrid/AsceSpatialGridSubsystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceBuildingDebugger)

namespace
{
	/** 按索引取一个稳定但可区分的颜色（调试绘制用）。 */
	FORCEINLINE FColor IndexColor(int32 Index)
	{
		static const FColor Palette[] = {
			FColor::Cyan, FColor::Green, FColor::Yellow, FColor::Orange,
			FColor::Blue, FColor::Magenta, FColor::Red, FColor::Turquoise
		};
		return Palette[static_cast<uint32>(Index) % UE_ARRAY_COUNT(Palette)];
	}
}

AAsceBuildingDebugger::AAsceBuildingDebugger()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
}

bool AAsceBuildingDebugger::GetDrawCenter(FVector& OutCenterWorld, float& OutRange) const
{
	if (!bDrawOnlyAroundView)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return false;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	OutCenterWorld = ViewLocation;
	OutRange = FMath::Max(0.f, DrawRange);
	return true;
}

void AAsceBuildingDebugger::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bDrawFootprints && !bDrawCellLines && !bDrawCoarseCellCount)
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector CenterWorld;
	float Range = 0.f;
	const bool bUseRange = GetDrawCenter(CenterWorld, Range);

	// 建筑 footprint（从建筑系统的"建筑集合"读，和游戏内增删同源）
	const UAsceBuildingSubsystem* Buildings = UAsceBuildingSubsystem::Get(this);
	if (Buildings && (bDrawFootprints || bDrawCellLines))
	{
		const TArray<FAsceBuiltBuilding>& List = Buildings->GetBuildings();
		for (int32 i = 0; i < List.Num(); ++i)
		{
			const FAsceBuildingPlacement& Placement = List[i].Placement;
			if (Placement.BuildingDefId.IsNone())
			{
				continue;
			}

			FIntPoint Min, Max;
			Placement.GetOccupiedBounds(Min, Max);

			const FVector BoxCenter(
				(Min.X + Max.X + 1) * 0.5f * 100.f,
				(Min.Y + Max.Y + 1) * 0.5f * 100.f,
				DrawHeight);
			const FVector BoxExtent(
				(Max.X - Min.X + 1) * 0.5f * 100.f,
				(Max.Y - Min.Y + 1) * 0.5f * 100.f,
				20.f);

			if (bUseRange && FVector::DistSquared2D(BoxCenter, CenterWorld) > Range * Range)
			{
				continue;
			}

			const FColor Color = IndexColor(i);
			if (bDrawFootprints)
			{
				DrawDebugBox(World, BoxCenter, BoxExtent, Color, false, -1.f, 0, 3.f);
				DrawDebugString(World, BoxCenter + FVector(0.f, 0.f, 40.f),
					FString::Printf(TEXT("#%d rot=%d (%dx%d)"), i, Placement.Rotation, Max.X - Min.X + 1, Max.Y - Min.Y + 1),
					nullptr, Color, 0.f, true, 1.f);
			}
			if (bDrawCellLines)
			{
				for (int32 y = Min.Y; y <= Max.Y; ++y)
				{
					for (int32 x = Min.X; x <= Max.X; ++x)
					{
						const FVector CellCenter((x + 0.5f) * 100.f, (y + 0.5f) * 100.f, DrawHeight);
						DrawDebugBox(World, CellCenter, FVector(50.f, 50.f, 5.f), Color, false, -1.f, 0, 1.f);
					}
				}
			}
		}
	}

	DrawCoarseCellCounts(CenterWorld, bUseRange ? Range : 0.f);
}

void AAsceBuildingDebugger::DrawCoarseCellCounts(const FVector& CenterWorld, float Range) const
{
	if (!bDrawCoarseCellCount)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const UAsceSpatialGridSubsystem* Grid = UAsceSpatialGridSubsystem::Get(this);
	if (!World || !Grid || !Grid->IsBuildingsGridReady())
	{
		return;
	}

	// 只扫绘制范围内的逻辑格，按粗格去重后画"该粗格里的建筑数"
	const float CellSize = 100.f;
	const FIntPoint LogicalDim = Grid->GetBuildingsLogicalDim();
	int32 MinCellX = 0, MinCellY = 0, MaxCellX = LogicalDim.X - 1, MaxCellY = LogicalDim.Y - 1;
	if (Range > 0.f)
	{
		MinCellX = FMath::Clamp(FMath::FloorToInt((CenterWorld.X - Range) / CellSize), 0, LogicalDim.X - 1);
		MaxCellX = FMath::Clamp(FMath::CeilToInt((CenterWorld.X + Range) / CellSize), 0, LogicalDim.X - 1);
		MinCellY = FMath::Clamp(FMath::FloorToInt((CenterWorld.Y - Range) / CellSize), 0, LogicalDim.Y - 1);
		MaxCellY = FMath::Clamp(FMath::CeilToInt((CenterWorld.Y + Range) / CellSize), 0, LogicalDim.Y - 1);
	}

	TSet<FIntPoint> VisitedCoarse;
	const float CoarseSize = Grid->GetBuildingsCoarseCellSize();
	for (int32 y = MinCellY; y <= MaxCellY; ++y)
	{
		for (int32 x = MinCellX; x <= MaxCellX; ++x)
		{
			const FIntPoint Coarse = Grid->LogicalToCoarse(FIntPoint(x, y));
			if (VisitedCoarse.Contains(Coarse))
			{
				continue;
			}
			VisitedCoarse.Add(Coarse);

			const FAsceGridBuildingsData* Cell = Grid->GetBuildingsDataCell(Coarse);
			const int32 Count = Cell ? Cell->Buildings.Num() : 0;
			if (Count <= 0)
			{
				continue;
			}
			DrawDebugString(World,
				FVector((Coarse.X + 0.5f) * CoarseSize, (Coarse.Y + 0.5f) * CoarseSize, DrawHeight + 80.f),
				FString::Printf(TEXT("%d"), Count),
				nullptr, FColor::White, 0.f, true, 1.2f);
		}
	}
}

// ── 控制台命令 ──

void AAsceBuildingDebugger::BuildingDebugPlaceCell(int32 CellX, int32 CellY, int32 Rotation)
{
	UAsceBuildingSubsystem* Buildings = UAsceBuildingSubsystem::Get(this);
	if (!Buildings)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] 没有 UAsceBuildingSubsystem"));
		return;
	}
	if (TestConfig.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] 没设 TestConfig（建筑类型行名）"));
		return;
	}

	FAsceBuildingDefBase Def;
	if (!UAsceEntityDefLibrary::GetBuildingDefByName(TestConfig.RowName, Def))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] TestConfig=%s 在实体定义表里找不到（或不是建筑类型）"),
			*TestConfig.RowName.ToString());
		return;
	}

	const FAsceBuildingPlacement Placement = FAsceBuildingPlacement::MakeFromDefCenter(
		TestConfig.RowName,
		FIntPoint(FMath::Max(1, Def.SizeX), FMath::Max(1, Def.SizeY)),
		FIntPoint(CellX, CellY),
		Rotation,
		TestTeamKey);

	FString Error;
	if (Buildings->PlaceBuilding(Placement, &Error))
	{
		UE_LOG(LogTemp, Log, TEXT("[BuildingDebug] 建造成功：cell=(%d,%d) rot=%d def=%s"),
			CellX, CellY, Placement.Rotation, *TestConfig.RowName.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] 建造失败：%s"), *Error);
	}
}

void AAsceBuildingDebugger::BuildingDebugDemolishCell(int32 CellX, int32 CellY)
{
	UAsceBuildingSubsystem* Buildings = UAsceBuildingSubsystem::Get(this);
	if (!Buildings)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] 没有 UAsceBuildingSubsystem"));
		return;
	}

	const TArray<FAsceBuiltBuilding>& List = Buildings->GetBuildings();
	for (const FAsceBuiltBuilding& Record : List)
	{
		FIntPoint Min, Max;
		Record.Placement.GetOccupiedBounds(Min, Max);
		if (CellX >= Min.X && CellX <= Max.X && CellY >= Min.Y && CellY <= Max.Y)
		{
			FString Error;
			if (Buildings->DemolishBuilding(Record.Entity, &Error))
			{
				UE_LOG(LogTemp, Log, TEXT("[BuildingDebug] 拆除成功：cell=(%d,%d) 命中建筑占用 (%d,%d)-(%d,%d)"),
					CellX, CellY, Min.X, Min.Y, Max.X, Max.Y);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] 拆除失败：%s"), *Error);
			}
			return;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] cell=(%d,%d) 没有建筑"), CellX, CellY);
}

void AAsceBuildingDebugger::BuildingDebugDemolishAll()
{
	UAsceBuildingSubsystem* Buildings = UAsceBuildingSubsystem::Get(this);
	if (!Buildings)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] 没有 UAsceBuildingSubsystem"));
		return;
	}

	// 拷贝一份（拆除会改内部数组）
	TArray<FMassEntityHandle> Entities;
	for (const FAsceBuiltBuilding& Record : Buildings->GetBuildings())
	{
		Entities.Add(Record.Entity);
	}

	int32 Removed = 0;
	for (const FMassEntityHandle Entity : Entities)
	{
		FString Error;
		if (Buildings->DemolishBuilding(Entity, &Error))
		{
			++Removed;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] 拆除失败：%s"), *Error);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[BuildingDebug] 全部拆除：%d / %d"), Removed, Entities.Num());
}

void AAsceBuildingDebugger::BuildingDebugDump() const
{
	const UAsceBuildingSubsystem* Buildings = UAsceBuildingSubsystem::Get(this);
	if (!Buildings)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] 没有 UAsceBuildingSubsystem"));
		return;
	}

	const TArray<FAsceBuiltBuilding>& List = Buildings->GetBuildings();
	UE_LOG(LogTemp, Log, TEXT("[BuildingDebug] 建筑清单（%d 个；已引导=%s）"),
		List.Num(), Buildings->IsBootstrapped() ? TEXT("yes") : TEXT("no"));

	for (int32 i = 0; i < List.Num(); ++i)
	{
		const FAsceBuiltBuilding& Record = List[i];
		FIntPoint Min, Max;
		Record.Placement.GetOccupiedBounds(Min, Max);
		UE_LOG(LogTemp, Log, TEXT("  #%d entity=(%d,%d) def=%s origin=(%d,%d) size=(%d,%d) rot=%d team=%d occ=(%d,%d)-(%d,%d)"),
			i,
			Record.Entity.Index, Record.Entity.SerialNumber,
			*Record.Placement.BuildingDefId.ToString(),
			Record.Placement.OriginGridLocation.X, Record.Placement.OriginGridLocation.Y,
			Record.Placement.Size.X, Record.Placement.Size.Y,
			Record.Placement.Rotation, Record.Placement.TeamKey,
			Min.X, Min.Y, Max.X, Max.Y);
	}
}
