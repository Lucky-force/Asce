// AsceWorldMapReplicatorActor.cpp
#include "AsceWorldMapReplicatorActor.h"
#include "AscePathFindingSubsystem.h"
#include "AsceCostFieldChunkActor.h"
#include "Engine/World.h"
#include "AsceMass/Bake/AsceMapDims.h"
#include "AsceMass/Bake/AsceBakedMapData.h"
#include "Net/UnrealNetwork.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"
#include "AsceMass/SpatialGrid/AsceSpatialGridSubsystem.h"
#include "AsceMass/Simulation/AsceSimulationSubsystem.h"

AAsceWorldMapReplicatorActor::AAsceWorldMapReplicatorActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	// 每帧 drain 代价场复制队列（有客户端的权威端；队列空时零开销）
	PrimaryActorTick.bCanEverTick = true;
}

void AAsceWorldMapReplicatorActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// WorldMapData 不再参与复制：内容由 `AAsceCostFieldChunkActor` 分块复制（每块都 < 64KB bunch 上限）
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

	// 代价场复制的分流开关：只有"有客户端的权威端"（DS / ListenServer）**且开了对客户端复制**才走分帧队列；
	// Standalone / 客户端无网络动作（本地 dense 已即时写）。必须在下面应用烘焙/建层之前设置好。
	// 默认不复制（客户端不跑寻路/碰撞/流场，不需要代价数据）；开关见 bReplicateCostFieldsToClients。
	WorldMapData.bUsePacedReplication = HasAuthority()
		&& (GetNetMode() == NM_DedicatedServer || GetNetMode() == NM_ListenServer)
		&& bReplicateCostFieldsToClients;
	WorldMapData.ReplicatedUpdatesPerFrame = FMath::Max(1, CostFieldReplicatedUpdatesPerFrame);

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

	// 代价分层（BaseLand / Building / Special）：只在权威端建层，客户端只用复制下来的最终 CostFields。
	// 【步骤2】Base 层接线：直接抓"烘焙应用之后的当前代价"作为地形基线（未烘焙的地图抓到的就是初始值），
	// 层组合结果因此与当前 CostFields 严格相等，紧跟的 FlushAll 是一个可验证的空操作（ensure 自查）。
	// 注意：建筑的写入路径（footprint 观察者 / 技能 Special）本步仍未切到层组合，见后续步骤。
	if (HasAuthority())
	{
		LayeredCostField.InitLayeredCostField(static_cast<uint32>(FlowGridX), static_cast<uint32>(FlowGridY));

		const int32 GridNum = FlowGridX * FlowGridY;
		for (int32 i = 0; i < GridNum; ++i)
		{
			LayeredCostField.SetBaseCost(static_cast<uint32>(i), WorldMapData.CostFields[i].Cost);
		}

		// 对齐验证：BaseLand 快照 + 空 Building + 空 Special ⇒ 组合结果必须等于现值，FlushAll 不该有任何变更
		const bool bFlushChanged = LayeredCostField.FlushAll(WorldMapData);
		ensureMsgf(!bFlushChanged, TEXT("BaseLand 快照后 FlushAll 不应产生任何变更（层组合结果必须等于当前 CostFields）"));

		// 播种**贴墙代价**（地形墙也算）：半径 d 内 cost += (7 - d)，然后统一写回
		LayeredCostField.RebuildProximityCost(WorldMapData);
		LayeredCostField.FlushAll(WorldMapData);

		UE_LOG(LogFlowField, Log,
			TEXT("[AsceLayeredCostField] 建层 %u x %u，BaseLand 快照完成（Flush 变更=%s）（sizeof(TMap<uint32,FAsceSpecialCostCell>)=%d, sizeof(FAsceCostOp)=%d）"),
			static_cast<uint32>(FlowGridX), static_cast<uint32>(FlowGridY),
			bFlushChanged ? TEXT("yes(异常)") : TEXT("no"),
			static_cast<int32>(sizeof(TMap<uint32, FAsceSpecialCostCell>)),
			static_cast<int32>(sizeof(FAsceCostOp)));
	}

	// 初始化阶段（loading）就把墙簇表建好，不让碰撞解算在游戏内等待第一次重建：
	// - 权威端：SetCostField 已同步写了 CostFields，这里直接得到正确簇表；
	// - 客户端：先用默认（全通行）代价建一份空表，之后分块复制下来变墙时会再触发重建。
	WorldMapData.RebuildWallClusters();

	// 代价场分块复制：只有"有客户端的权威端"（DS / ListenServer）**且开了复制开关**才 spawn 分块；
	// Standalone 无远端客户端，不需要分块 actor。建图阶段的入队已无意义（预填已带最终值）⇒ 清空队列。
	if (WorldMapData.bUsePacedReplication)
	{
		DestroyCostFieldChunks();
		SpawnCostFieldChunks();
		WorldMapData.ResetReplicatedCostQueue();
	}

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

		// 建筑层：粗格覆盖整张逻辑地图（逻辑格 = costfield 格 = GRID_SIZE = 100cm；粗格取 4×4 逻辑格）
		SpatialGrid->ConstructBuildingsGrid(
			FIntPoint(FlowGridX, FlowGridY),
			static_cast<int32>(GRID_SIZE),
			4);
	}

	UE_LOG(LogTemp, Log, TEXT("[AsceWorldMap] 初始化地图：%d x %d 扇区，流场格 %d x %d，视野格 %d x %d（Level=%d, Scale=%d, %.1fcm/格, Baked=%s）"),
		SectorX, SectorY, FlowGridX, FlowGridY, VisionGridX, VisionGridY,
		VisionDetailLevel, Scale, VisionCellCm, bBakedDimsOk ? TEXT("yes") : TEXT("no"));

	// 路径地图注册（原来在 BeginPlay 里，挪进来让"世界初始化"是一个完整动作，由调度器调用）
	if (UAscePathFindingSubsystem* Subsystem = UAscePathFindingSubsystem::Get(this))
	{
		Subsystem->RegisterWorldMapReplicator(this);
	}
}

void AAsceWorldMapReplicatorActor::SpawnCostFieldChunks()
{
	const int32 GridNum = WorldMapData.CostFields.Num();
	if (GridNum <= 0)
	{
		return;
	}

	// 动态分块：16 格/条；目标 actor ≤ 8，单块 1024~2048 条
	// （线上每条 ≈24B ⇒ 单块初发 ≤48KB，离 64KB bunch 上限留 25% 余量）
	static constexpr int32 CellsPerPack = FAsceCostFieldPack::CellsPerPack;
	static constexpr int32 MinPacksPerActor = 1024;
	static constexpr int32 MaxPacksPerActor = 2048;
	static constexpr int32 TargetMaxActors = 8;

	const int32 TotalPacks = FMath::DivideAndRoundUp(GridNum, CellsPerPack);
	const int32 PacksPerActor = FMath::Clamp(
		FMath::DivideAndRoundUp(TotalPacks, TargetMaxActors), MinPacksPerActor, MaxPacksPerActor);
	const int32 ActorCount = FMath::DivideAndRoundUp(TotalPacks, PacksPerActor);
	CostFieldChunkCells = PacksPerActor * CellsPerPack;

	CostFieldChunks.Reset();
	CostFieldChunks.Reserve(ActorCount);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (int32 i = 0; i < ActorCount; ++i)
	{
		const int32 StartCell = i * CostFieldChunkCells;
		const int32 CellCount = FMath::Min(CostFieldChunkCells, GridNum - StartCell);
		if (CellCount <= 0)
		{
			break;
		}

		FActorSpawnParameters Params;
		Params.Owner = this;
		AAsceCostFieldChunkActor* Chunk = World->SpawnActor<AAsceCostFieldChunkActor>(
			AAsceCostFieldChunkActor::StaticClass(), FTransform::Identity, Params);
		if (!Chunk)
		{
			continue;
		}

		Chunk->ConfigureFromWorld(StartCell, CellCount, WorldMapData);
		CostFieldChunks.Add(Chunk);
	}

	UE_LOG(LogTemp, Log, TEXT("[AsceWorldMap] 代价场分块复制：%d 格 → %d 块（每块 %d 格 / %d 条，线上约 %.0fKB/块）"),
		GridNum, CostFieldChunks.Num(), CostFieldChunkCells, PacksPerActor,
		static_cast<double>(PacksPerActor * 24) / 1024.0);
}

void AAsceWorldMapReplicatorActor::DestroyCostFieldChunks()
{
	for (TObjectPtr<AAsceCostFieldChunkActor>& Chunk : CostFieldChunks)
	{
		if (IsValid(Chunk))
		{
			Chunk->Destroy();
		}
	}
	CostFieldChunks.Reset();
	CostFieldChunkCells = 0;
}

void AAsceWorldMapReplicatorActor::RouteCostFieldUpdate(uint32 GridIndex)
{
	if (CostFieldChunkCells <= 0 || CostFieldChunks.Num() == 0)
	{
		return;
	}
	const int32 ChunkIndex = static_cast<int32>(GridIndex) / CostFieldChunkCells;
	if (!CostFieldChunks.IsValidIndex(ChunkIndex) || !IsValid(CostFieldChunks[ChunkIndex]))
	{
		return;
	}
	if (!WorldMapData.CostFields.IsValidIndex(static_cast<int32>(GridIndex)))
	{
		return;
	}
	CostFieldChunks[ChunkIndex]->SetCellCostFromWorld(GridIndex, WorldMapData.CostFields[GridIndex].Cost);
}

void AAsceWorldMapReplicatorActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 世界数据时间片处理（两端都跑）：扇区/Portal/Island/墙簇/雾 全靠它消费 PendingUpdateCostFields；
	// 客户端收到的分块复制也走同一条链。放在这里而不是 debugger 里，保证没有 debugger 的端也正常处理。
	WorldMapData.UpdateWorld(DeltaSeconds);

	// 代价场分帧队列：每帧按预算 drain，把格变更路由到所属分块 actor（有客户端的权威端；
	// Standalone/客户端在方法内部直接早退，队列空时零开销）
	WorldMapData.DrainReplicatedCostUpdates([this](uint32 GridIndex)
	{
		RouteCostFieldUpdate(GridIndex);
	});
}

void AAsceWorldMapReplicatorActor::BeginPlay()
{
	Super::BeginPlay();

	// 只注册自己：世界数据的实际初始化由 `UAsceSimulationSubsystem` 按固定时机调用（`InitializeMapDimensions`），
	// 这样"世界数据 → 雾高度场 → 网格构建 → 路径地图注册"的顺序由调度器统一管控，而不是压在一个 Actor 的 BeginPlay 上。
	if (UAsceSimulationSubsystem* Sim = UAsceSimulationSubsystem::Get(this))
	{
		Sim->RegisterWorldDataSource(this);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[AsceWorldMap] 找不到 UAsceSimulationSubsystem：世界数据未初始化"));
	}
}

void AAsceWorldMapReplicatorActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyCostFieldChunks();

	if (UAsceSimulationSubsystem* Sim = UAsceSimulationSubsystem::Get(this))
	{
		Sim->UnregisterWorldDataSource(this);
	}
	if (UAscePathFindingSubsystem* Subsystem = UAscePathFindingSubsystem::Get(this))
	{
		Subsystem->UnregisterWorldMapReplicator(this);
	}
	Super::EndPlay(EndPlayReason);
}
