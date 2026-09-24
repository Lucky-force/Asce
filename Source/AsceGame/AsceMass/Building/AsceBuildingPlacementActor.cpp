// AsceBuildingPlacementActor.cpp
#include "AsceBuildingPlacementActor.h"

#include "AsceBuildingSubsystem.h"
#include "AsceMass/Building/AsceBuildingDef.h"
#include "AsceMass/Data/AsceEntityDefLibrary.h"
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

	// 尺寸来自实体定义表（拿不到就 1×1；落地时 ValidatePlacement 会再拦）
	FIntPoint Size(1, 1);
	if (FAsceBuildingDefBase Def; UAsceEntityDefLibrary::GetBuildingDefByName(BuildingDef.RowName, Def))
	{
		Size = FIntPoint(FMath::Max(1, Def.SizeX), FMath::Max(1, Def.SizeY));
	}

	return FAsceBuildingPlacement::MakeFromDefCenter(BuildingDef.RowName, Size, CenterCell, Rotation, TeamId);
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

	// 位置吸附：把"本次拖动增量"先累积到虚拟自由位置，再按它算所在格的中心。
	// 直接按当前位置吸附会把增量吃掉（拖动距离不累积、拖不动）——见头文件成员注释。
	const FVector Location = GetActorLocation();
	const FVector2D RawLocation(Location.X, Location.Y);
	if (!bEditorFreeLocationInitialized)
	{
		EditorFreeLocation = RawLocation;
		bEditorFreeLocationInitialized = true;
	}
	else
	{
		EditorFreeLocation += RawLocation - EditorLastSnappedLocation;
	}

	const FIntPoint CenterCell(
		FMath::FloorToInt(EditorFreeLocation.X / GRID_SIZE),
		FMath::FloorToInt(EditorFreeLocation.Y / GRID_SIZE));
	const FVector SnappedLocation(
		(CenterCell.X + 0.5) * GRID_SIZE,
		(CenterCell.Y + 0.5) * GRID_SIZE,
		Location.Z);
	EditorLastSnappedLocation = FVector2D(SnappedLocation.X, SnappedLocation.Y);
	if (!SnappedLocation.Equals(Location, 0.5))
	{
		SetActorLocation(SnappedLocation);
	}

	// 朝向吸附：同上，先累积再吸附到 90° 的倍数（保证"视觉 == 逻辑 Rotation"）。
	const float RawYaw = GetActorRotation().Yaw;
	if (!bEditorFreeYawInitialized)
	{
		EditorFreeYaw = RawYaw;
		bEditorFreeYawInitialized = true;
	}
	else
	{
		EditorFreeYaw += FRotator::NormalizeAxis(RawYaw - EditorLastSnappedYaw);
	}

	const float SnappedYaw = FMath::RoundToFloat(EditorFreeYaw / 90.f) * 90.f;
	const float DisplayYaw = FRotator::NormalizeAxis(SnappedYaw);
	EditorLastSnappedYaw = DisplayYaw;
	if (!FMath::IsNearlyEqual(RawYaw, DisplayYaw, 0.1f))
	{
		SetActorRotation(FRotator(0.f, DisplayYaw, 0.f));
	}

	// 占格标记：**未旋转**尺寸 × GRID_SIZE（旋转由 Actor 的 Yaw 表现）
	int32 SizeX = 1;
	int32 SizeY = 1;
	if (FAsceBuildingDefBase Def; UAsceEntityDefLibrary::GetBuildingDefByName(BuildingDef.RowName, Def))
	{
		SizeX = FMath::Max(1, Def.SizeX);
		SizeY = FMath::Max(1, Def.SizeY);
	}
	if (FootprintVisual)
	{
		FootprintVisual->SetRelativeScale3D(FVector(
			static_cast<float>(SizeX),
			static_cast<float>(SizeY),
			1.f));
	}
}
#endif
