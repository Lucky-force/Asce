// AsceWorldMapReplicatorActor.cpp
#include "AsceWorldMapReplicatorActor.h"
#include "AscePathFindingSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"
#include "AsceMass/SpatialGrid/AsceSpatialGridSubsystem.h"

AAsceWorldMapReplicatorActor::AAsceWorldMapReplicatorActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = false;
}

void AAsceWorldMapReplicatorActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAsceWorldMapReplicatorActor, WorldMapData);
}

int32 AAsceWorldMapReplicatorActor::GetVisionScale() const
{
	// 按当前 GRID_SIZE 现算可用 Scale 表：所有能整除 GRID_SIZE、>1 且为偶数的因子，升序。
	// 保证 VisionCell = GRID_SIZE / Scale 为整数。设置/初始化时才调用，开销无所谓。
	const int32 GridSize = FMath::Max(1, FMath::RoundToInt32(AsceVision::FlowFieldGridSizeCm));

	TArray<int32> Scales;
	Scales.Reserve(16);
	for (int32 S = 2; S <= GridSize; S += 2)
	{
		if ((GridSize % S) == 0)
		{
			Scales.Add(S);
		}
	}
	if (Scales.Num() == 0)
	{
		return 2; // 理论上 GRID_SIZE>1 且为偶数时至少有一个因子
	}

	const int32 Idx = FMath::Clamp(VisionDetailLevel, 1, Scales.Num()) - 1;
	return Scales[Idx];
}

void AAsceWorldMapReplicatorActor::AlignSectorCounts(int32& OutSectorX, int32& OutSectorY) const
{
	// 视野网格宽 = SectorCount * SECTOR_SIZE * Scale。
	// 要求它是 TileCells 的整数倍：既保证行字对齐(GridW%32==0)，又保证脏 tile 完整(无边界行/列)，
	// 这样 ExtractTileWords 可以直接按行 memcpy。向上取整扇区数到满足该约束的最小步长。
	const int32 Scale = GetVisionScale();
	const int32 Factor = FMath::Max(1, SECTOR_SIZE * Scale); // 每扇区对应的视野格数
	const int32 AlignUnit = UAsceFogSubsystem::TileCells;    // 64
	const int32 Gcd = FMath::Max(1, FMath::GreatestCommonDivisor(Factor, AlignUnit));
	const int32 Step = FMath::Max(1, AlignUnit / Gcd);

	OutSectorX = FMath::Max(1, FMath::DivideAndRoundUp(SectorCountX, Step) * Step);
	OutSectorY = FMath::Max(1, FMath::DivideAndRoundUp(SectorCountY, Step) * Step);
}

void AAsceWorldMapReplicatorActor::InitializeMapDimensions()
{
	int32 SectorX = SectorCountX;
	int32 SectorY = SectorCountY;
	AlignSectorCounts(SectorX, SectorY);
	SectorCountX = SectorX;
	SectorCountY = SectorY;

	// 两端都初始化地图数据：尺寸/扇区结构由本 Actor 的配置在两端一致地本地初始化，
	// CostFields 的"内容"（代价/墙）才是权威端独有、通过 FastArray 复制覆盖的部分。
	// （客户端也需要 GridCountX/Y 等结构来解析复制的 CostFields 索引，否则寻路/墙簇是空图。）
	// TODO: 关卡静态障碍物/地形分析结果灌入WorldMapData的真正入口（权威端）。
	WorldMapData.InitWorldMapData(
		static_cast<uint8>(SectorX), static_cast<uint8>(SectorY));

	// 两端都做：由扇区数派生维度写入 Fog / SpatialGrid
	const int32 Scale = GetVisionScale();
	const int32 FlowGridX = SectorX * SECTOR_SIZE;
	const int32 FlowGridY = SectorY * SECTOR_SIZE;
	const float VisionCellCm = AsceVision::FlowFieldGridSizeCm / static_cast<float>(Scale);
	AsceVision::ValidateCellSizeConfig(VisionCellCm);

	if (UAsceFogSubsystem* Fog = UAsceFogSubsystem::Get(this))
	{
		Fog->InitializeVisionMap(FlowGridX * Scale, FlowGridY * Scale, VisionCellCm);
	}

	if (UAsceSpatialGridSubsystem* SpatialGrid = UAsceSpatialGridSubsystem::Get(this))
	{
		const FVector2D WorldEnd(
			FlowGridX * GRID_SIZE,
			FlowGridY * GRID_SIZE);
		SpatialGrid->ConstructGrid(FVector2D::ZeroVector, WorldEnd, SpatialMinEntityRadius);
	}

	UE_LOG(LogTemp, Log, TEXT("[AsceWorldMap] 初始化地图：%d x %d 扇区，流场格 %d x %d，视野格 %d x %d（Level=%d, Scale=%d, %.1fcm/格）"),
		SectorX, SectorY, FlowGridX, FlowGridY, FlowGridX * Scale, FlowGridY * Scale,
		VisionDetailLevel, Scale, VisionCellCm);
}

void AAsceWorldMapReplicatorActor::BeginPlay()
{
	Super::BeginPlay();

	InitializeMapDimensions();

	if (UAscePathFindingSubsystem* Subsystem = UAscePathFindingSubsystem::Get(this))
	{
		Subsystem->RegisterWorldMapReplicator(this);
	}
}

void AAsceWorldMapReplicatorActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAscePathFindingSubsystem* Subsystem = UAscePathFindingSubsystem::Get(this))
	{
		Subsystem->UnregisterWorldMapReplicator(this);
	}
	Super::EndPlay(EndPlayReason);
}
