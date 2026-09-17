// AsceBuildingPlacementActor.cpp
#include "AsceBuildingPlacementActor.h"

#include "AsceBuildingSubsystem.h"
#include "AsceMass/Ether/AsceEtherBuildingConfig.h"
#include "AsceMass/FlowField/AsceFlowFieldCores.h"   // GRID_SIZE
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AAsceBuildingPlacementActor::AAsceBuildingPlacementActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	FootprintVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FootprintVisual"));
	RootComponent = FootprintVisual;
	FootprintVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FootprintVisual->SetHiddenInGame(true);
	FootprintVisual->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		FootprintVisual->SetStaticMesh(CubeMesh.Object);
	}
}

FAsceBuildingPlacement AAsceBuildingPlacementActor::BuildPlacement() const
{
	// 位置（cm）→ 所在逻辑格 = footprint 中心格；旋转 = Yaw 顺时针 90° 次数
	const FVector Location = GetActorLocation();
	const FIntPoint CenterCell(
		FMath::FloorToInt(Location.X / GRID_SIZE),
		FMath::FloorToInt(Location.Y / GRID_SIZE));
	const int32 Rotation = ((FMath::RoundToInt(GetActorRotation().Yaw / 90.f) % 4) + 4) % 4;

	return FAsceBuildingPlacement::MakeFromConfigCenter(BuildingConfig, CenterCell, Rotation, TeamKey, BuildingDefId);
}

void AAsceBuildingPlacementActor::BeginPlay()
{
	Super::BeginPlay();

	if (!bEnabled)
	{
		return;
	}

	if (UAsceBuildingSubsystem* Sub = UAsceBuildingSubsystem::Get(this))
	{
		Sub->RegisterPlacement(this);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Building] 摆放 Actor %s：找不到 UAsceBuildingSubsystem，未注册"), *GetName());
	}
}

#if WITH_EDITOR
void AAsceBuildingPlacementActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 吸附：位置 → 所在格中心；Yaw → 90° 的倍数（保证"视觉 == 逻辑 Rotation"）
	const FVector Location = GetActorLocation();
	const FIntPoint CenterCell(
		FMath::FloorToInt(Location.X / GRID_SIZE),
		FMath::FloorToInt(Location.Y / GRID_SIZE));
	const FVector SnappedLocation(
		(CenterCell.X + 0.5) * GRID_SIZE,
		(CenterCell.Y + 0.5) * GRID_SIZE,
		Location.Z);
	if (!SnappedLocation.Equals(Location, 0.5))
	{
		SetActorLocation(SnappedLocation);
	}

	const float Yaw = GetActorRotation().Yaw;
	const float SnappedYaw = FMath::RoundToFloat(Yaw / 90.f) * 90.f;
	if (!FMath::IsNearlyEqual(Yaw, SnappedYaw, 0.1f))
	{
		SetActorRotation(FRotator(0.f, SnappedYaw, 0.f));
	}

	// 占格标记：**未旋转**尺寸 × GRID_SIZE（旋转由 Actor 的 Yaw 表现）
	const UAsceEtherBuildingConfig* Cfg = BuildingConfig ? BuildingConfig.GetDefaultObject() : nullptr;
	if (FootprintVisual && Cfg)
	{
		FootprintVisual->SetRelativeScale3D(FVector(
			static_cast<float>(FMath::Max(1, Cfg->SizeX)),
			static_cast<float>(FMath::Max(1, Cfg->SizeY)),
			1.f));
	}
}
#endif
