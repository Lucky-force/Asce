// AsceWorldMapReplicatorActor.cpp
#include "AsceWorldMapReplicatorActor.h"
#include "AscePathFindingSubsystem.h"
#include "AsceMass/Bake/AsceMapDims.h"
#include "AsceMass/Bake/AsceBakedMapData.h"
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
	return AsceMapDims::GetVisionScale(VisionDetailLevel);
}

void AAsceWorldMapReplicatorActor::AlignSectorCounts(int32& OutSectorX, int32& OutSectorY) const
{
	AsceMapDims::AlignSectorCounts(OutSectorX, OutSectorY, GetVisionScale(), UAsceFogSubsystem::TileCells);
}

void AAsceWorldMapReplicatorActor::InitializeMapDimensions()
{
	int32 SectorX = SectorCountX;
	int32 SectorY = SectorCountY;
	AlignSectorCounts(SectorX, SectorY);
	SectorCountX = SectorX;
	SectorCountY = SectorY;

	const int32 Scale = GetVisionScale();
	const int32 FlowGridX = SectorX * SECTOR_SIZE;
	const int32 FlowGridY = SectorY * SECTOR_SIZE;
	const int32 VisionGridX = FlowGridX * Scale;
	const int32 VisionGridY = FlowGridY * Scale;
	const float VisionCellCm = AsceVision::FlowFieldGridSizeCm / static_cast<float>(Scale);
	AsceVision::ValidateCellSizeConfig(VisionCellCm);

	// 校验烘焙数据尺寸与当前配置一致（不一致就不应用，避免踩内存）
	const bool bBaked = BakedMapData != nullptr;
	const bool bBakedDimsOk = bBaked
		&& BakedMapData->SectorCountX == SectorX && BakedMapData->SectorCountY == SectorY
		&& BakedMapData->FlowGridW == FlowGridX && BakedMapData->FlowGridH == FlowGridY
		&& BakedMapData->VisionGridW == VisionGridX && BakedMapData->VisionGridH == VisionGridY;
	if (bBaked && !bBakedDimsOk)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AsceWorldMap] BakedMapData 尺寸与配置不匹配，忽略烘焙数据（baked %dx%d / flow %dx%d / vis %dx%d）"),
			BakedMapData->SectorCountX, BakedMapData->SectorCountY,
			BakedMapData->FlowGridW, BakedMapData->FlowGridH,
			BakedMapData->VisionGridW, BakedMapData->VisionGridH);
	}

	// 两端都初始化地图数据：尺寸/扇区结构由本 Actor 的配置在两端一致地本地初始化，
	// CostFields 的"内容"（代价/墙）才是权威端独有、通过 FastArray 复制覆盖的部分。
	// （客户端也需要 GridCountX/Y 等结构来解析复制的 CostFields 索引，否则寻路/墙簇是空图。）
	WorldMapData.InitWorldMapData(
		static_cast<uint8>(SectorX), static_cast<uint8>(SectorY));

	// 权威端：应用烘焙阻挡（CostFields 内容为权威端独有）
	bBakedDataApplied = false;
	if (HasAuthority() && bBakedDimsOk)
	{
		const int32 FlowCount = FlowGridX * FlowGridY;
		for (int32 i = 0; i < FlowCount; ++i)
		{
			const uint8 Cost = (i < BakedMapData->Blocked.Num() && BakedMapData->Blocked[i])
				? static_cast<uint8>(0b10000000) // 墙位
				: static_cast<uint8>(1);         // 可通行
			WorldMapData.SetCostField(static_cast<uint32>(i), Cost);
		}
		bBakedDataApplied = true;
	}

	// 初始化阶段（loading）就把墙簇表建好，不让碰撞解算在游戏内等待第一次重建：
	// - 权威端：SetCostField 已同步写了 CostFields，这里直接得到正确簇表；
	// - 客户端：先用默认（全通行）代价建一份空表，之后 CostFields 复制下来变墙时会再触发重建。
	WorldMapData.RebuildWallClusters();

	// 两端都做：写入视野高度场（本地数据，不复制）
	if (UAsceFogSubsystem* Fog = UAsceFogSubsystem::Get(this))
	{
		Fog->InitializeVisionMap(VisionGridX, VisionGridY, VisionCellCm);

		if (bBakedDimsOk && Fog->GetHeightMap().IsValid())
		{
			TArray<uint16> Heights = BakedMapData->StaticHeights; // 拷贝，避免 move 掉资产数据
			Fog->GetHeightMap().SetStaticHeights(MoveTemp(Heights));
		}
	}

	if (UAsceSpatialGridSubsystem* SpatialGrid = UAsceSpatialGridSubsystem::Get(this))
	{
		const FVector2D WorldEnd(
			FlowGridX * GRID_SIZE,
			FlowGridY * GRID_SIZE);
		SpatialGrid->ConstructGrid(FVector2D::ZeroVector, WorldEnd, SpatialMinEntityRadius);
	}

	UE_LOG(LogTemp, Log, TEXT("[AsceWorldMap] 初始化地图：%d x %d 扇区，流场格 %d x %d，视野格 %d x %d（Level=%d, Scale=%d, %.1fcm/格, Baked=%s）"),
		SectorX, SectorY, FlowGridX, FlowGridY, VisionGridX, VisionGridY,
		VisionDetailLevel, Scale, VisionCellCm, bBakedDimsOk ? TEXT("yes") : TEXT("no"));
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
