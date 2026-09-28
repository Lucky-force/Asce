// Fill out your copyright notice in the Description page of Project Settings.
#if WITH_EDITOR
#include "AsceFlowFieldDebugger.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Components/DecalComponent.h"
#include "GameFramework/PlayerController.h"
#include "AsceMass/FlowField/AscePathFindingSubsystem.h"
#include "AsceMass/FlowField/AsceWorldMapReplicatorActor.h"
#include "AsceMass/Components/AsceRTSCommanderComponent.h"

namespace
{
	/** 调试器修改"地形"：写 BaseLand 层 + flush（没有分层时退化为直写 CostFields）。 */
	void SetDebugTerrainCost(FWorldMapData& WorldMapData, FAsceLayeredCostField* LayeredCostField, uint32 GridIndex, uint8 NewCost)
	{
		if (LayeredCostField)
		{
			LayeredCostField->SetBaseCost(GridIndex, NewCost);
			LayeredCostField->FlushCell(GridIndex, WorldMapData);
		}
		else
		{
			WorldMapData.SetCostField(GridIndex, NewCost);
		}
	}
}

AAsceFlowFieldDebugger::AAsceFlowFieldDebugger(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>("Root");
	DebugDecal = CreateDefaultSubobject<UDecalComponent>("DecalComponent");
	DebugDecal->SetupAttachment(RootComponent);
	// 平躺投影到地面
	DebugDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));

	// 默认测试墙布局：三块矩形 + 一条竖墙，形成走廊
	TestWallRects.Add(FVector4(8, 8, 4, 8));     // 左上方块
	TestWallRects.Add(FVector4(16, 24, 8, 4));   // 右下横条
	TestWallRects.Add(FVector4(32, 8, 4, 16));   // 右上方块
}

void AAsceFlowFieldDebugger::BeginPlay()
{
	Super::BeginPlay();

	// 地图数据/PathFinder 现在由关卡摆放的 AAsceWorldMapReplicatorActor + UAscePathFindingSubsystem 提供，
	// 本调试器不再自建。Actor 的 BeginPlay 顺序不确定，故先尝试绑定，失败则 Tick 里重试。
	TryBindMap();
}

void AAsceFlowFieldDebugger::TryBindMap()
{
	if (bMapBound)
	{
		return;
	}

	UAscePathFindingSubsystem* PathFindingSubsystem = UAscePathFindingSubsystem::Get(this);
	AAsceWorldMapReplicatorActor* Replicator =
		PathFindingSubsystem ? PathFindingSubsystem->GetWorldMapReplicator() : nullptr;
	if (!Replicator)
	{
		return; // 还没就绪，等下一帧
	}

	// 复用权威地图与共享 PathFinder
	WorldMapData = &Replicator->WorldMapData;
	PathFinder = PathFindingSubsystem->GetPathFinder();
	LayeredCostField = &Replicator->LayeredCostField;
	bMapAuthority = Replicator->HasAuthority();
	bMapBound = true;

	// 监听世界数据变更（本地可视更新）
	WorldMapData->OnCostFieldChanged.AddUObject(this, &AAsceFlowFieldDebugger::OnCostFieldChanged);
	WorldMapData->OnStructureChanged.AddUObject(this, &AAsceFlowFieldDebugger::OnStructureChanged);

	// 占位地图初始化（仅权威端，且 Replicator 未应用烘焙数据时）：全量可通行 + 测试墙。
	// 若 Replicator 已应用真实烘焙地图（BakedMapData），这里不能再占地初始化——
	// 否则会把烘焙出来的墙全部擦成可通行（两个初始化源的冲突，Actor BeginPlay 顺序还不保证）。
	// 写入统一走 BaseLand 层（等同"手工改地形"）：这样建筑层 Add/Remove flush 时能正确还原地形原值。
	if (Replicator->HasAuthority() && !Replicator->HasBakedDataApplied())
	{
		if (LayeredCostField)
		{
			const int32 TotalGrids = WorldMapData->GetGridCountX() * WorldMapData->GetGridCountY();
			for (int32 GridIndex = 0; GridIndex < TotalGrids; ++GridIndex)
			{
				LayeredCostField->SetBaseCost(static_cast<uint32>(GridIndex), AsceCostField::PlainPassableCost);
			}

			if (bGenerateTestWalls)
			{
				for (const FVector4& Rect : TestWallRects)
				{
					const int32 StartX = FMath::FloorToInt32(Rect.X);
					const int32 StartY = FMath::FloorToInt32(Rect.Y);
					const int32 Width  = FMath::FloorToInt32(Rect.Z);
					const int32 Height = FMath::FloorToInt32(Rect.W);
					for (int32 Y = StartY; Y < StartY + Height; ++Y)
					{
						for (int32 X = StartX; X < StartX + Width; ++X)
						{
							LayeredCostField->SetBaseCost(
								WorldMapData->GridLocationToGridIndex(FIntVector2(X, Y)), AsceCostField::BlockedCost);
						}
					}
				}
			}

			LayeredCostField->FlushAll(*WorldMapData);
		}
	}

	BindDebugInput();
	BindToSelectionSource();

	bNeedsRedraw = true;
}

void AAsceFlowFieldDebugger::BindToSelectionSource()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		SelectionSourceCommander = nullptr;
		OnSelectionChangedHandle.Reset();
		return;
	}

	// BeginPlay时PlayerController往往还没有Pawn（RTS里Pawn是后续spawn/拥有出来的），
	// 用OnPossessedPawnChanged等Pawn上场/换绑时再找Commander绑定。
	PC->OnPossessedPawnChanged.RemoveAll(this);
	PC->OnPossessedPawnChanged.AddDynamic(this, &AAsceFlowFieldDebugger::OnPawnPossessedChanged);

	// 有可能Pawn已经存在（关卡直接放置的Pawn），主动试一次
	OnPawnPossessedChanged(PC->GetPawn(), nullptr);
}

void AAsceFlowFieldDebugger::OnPawnPossessedChanged(APawn* OldPawn, APawn* NewPawn)
{
	// 先解绑旧的
	if (SelectionSourceCommander.IsValid())
	{
		SelectionSourceCommander->OnSelectionChanged.Remove(OnSelectionChangedHandle);
	}
	SelectionSourceCommander = nullptr;
	OnSelectionChangedHandle.Reset();

	UAsceRTSCommanderComponent* Commander = NewPawn ? NewPawn->FindComponentByClass<UAsceRTSCommanderComponent>() : nullptr;
	if (!Commander)
	{
		// 没有Commander：source为空（RefreshSourcesFromSelection也会走到清空分支）
		RefreshSourcesFromSelection();
		return;
	}

	SelectionSourceCommander = Commander;
	OnSelectionChangedHandle = Commander->OnSelectionChanged.AddUObject(this, &AAsceFlowFieldDebugger::OnSelectionChanged);
	// 绑定后立即拿一次当前选中（可能已有选中）
	OnSelectionChanged();
}

void AAsceFlowFieldDebugger::OnSelectionChanged()
{
	RefreshSourcesFromSelection();
}

void AAsceFlowFieldDebugger::RefreshSourcesFromSelection()
{
	// 用选中单位的位置作为合并A*的source（同格合并由RunMergeablePathRequest内部完成）
	SourceWorldPositions.Reset();
	TArray<FVector2D> EntityLocations;
	if (SelectionSourceCommander.IsValid())
	{
		SelectionSourceCommander->GetSelectedEntityLocations(EntityLocations);
	}
	SourceWorldPositions = MoveTemp(EntityLocations);

	// source变了，旧路径结果作废，需要重绘
	bNeedsRedraw = true;
	bMapTextureDirty = true;

	// 清掉旧的Plan句柄（换成下一次右键时新计算）
	if (LastPathPlanHandle.IsValid() && PathFinder)
	{
		PathFinder->ReleasePathPlan(LastPathPlanHandle);
	}
	LastPathPlanHandle = FMergedPathPlanHandle();
	LastPathResults.Reset();
	LastFlowFieldResults.Reset();
	bHasPathResult = false;
}

void AAsceFlowFieldDebugger::BindDebugInput()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && PC->InputComponent)
	{
		PC->InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AAsceFlowFieldDebugger::OnRightMousePressed);
		PC->InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AAsceFlowFieldDebugger::OnSpacePressed);
	}
}

void AAsceFlowFieldDebugger::OnSpacePressed()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}

	FHitResult Hit;
	if (PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		SourceWorldPositions.Add(FVector2D(Hit.Location.X, Hit.Location.Y));
	bNeedsRedraw = true;
	bMapTextureDirty = true; // LOS/积分结果变化，重写地图纹理
}
}

void AAsceFlowFieldDebugger::FlowFieldDebugClearSources()
{
	SourceWorldPositions.Reset();
	LastPathResults.Reset();
	LastFlowFieldResults.Reset();
	if (LastPathPlanHandle.IsValid() && PathFinder)
	{
		PathFinder->ReleasePathPlan(LastPathPlanHandle);
	}
	LastPathPlanHandle = FMergedPathPlanHandle();
	bHasPathResult = false;
	bNeedsRedraw = true;
	bMapTextureDirty = true; // 清掉LOS/积分显示
}

void AAsceFlowFieldDebugger::FlowFieldDebugAddSpecial(float WorldX, float WorldY, int32 Op, int32 ID)
{
	UAscePathFindingSubsystem* PathFinding = UAscePathFindingSubsystem::Get(this);
	if (!PathFinding)
	{
		return;
	}

	const FIntVector2 GridLoc = FWorldMapData::WorldLocationToGridLocation(FVector2D(WorldX, WorldY));
	const uint64 Handle = PathFinding->AddSpecialCostOp(
		GridLoc.X, GridLoc.Y, static_cast<EAsceCostOp>(Op), static_cast<uint32>(ID));
	if (Handle != FAsceLayeredCostField::InvalidHandle)
	{
		DebugSpecialHandles.Add(ID, Handle);
	}
	UE_LOG(LogFlowField, Log, TEXT("[DebugSpecial] Add op=%d id=%d grid(%d,%d) handle=%llu"),
		Op, ID, GridLoc.X, GridLoc.Y, Handle);
}

void AAsceFlowFieldDebugger::FlowFieldDebugRemoveSpecial(int32 ID)
{
	UAscePathFindingSubsystem* PathFinding = UAscePathFindingSubsystem::Get(this);
	uint64 Handle = FAsceLayeredCostField::InvalidHandle;
	if (!PathFinding || !DebugSpecialHandles.RemoveAndCopyValue(ID, Handle))
	{
		UE_LOG(LogFlowField, Warning, TEXT("[DebugSpecial] 找不到 id=%d 的贡献"), ID);
		return;
	}

	PathFinding->RemoveSpecialCostOp(Handle);
	UE_LOG(LogFlowField, Log, TEXT("[DebugSpecial] Remove id=%d handle=%llu"), ID, Handle);
}

void AAsceFlowFieldDebugger::FlowFieldDebugDumpCell(float WorldX, float WorldY)
{
	if (!WorldMapData || !LayeredCostField)
	{
		UE_LOG(LogFlowField, Warning, TEXT("[DebugSpecial] 地图/代价分层尚未就绪"));
		return;
	}

	const FIntVector2 GridLoc = FWorldMapData::WorldLocationToGridLocation(FVector2D(WorldX, WorldY));
	const int32 GridCountX = WorldMapData->GetGridCountX();
	const int32 GridCountY = WorldMapData->GetGridCountY();
	if (GridLoc.X < 0 || GridLoc.Y < 0 || GridLoc.X >= GridCountX || GridLoc.Y >= GridCountY)
	{
		UE_LOG(LogFlowField, Warning, TEXT("[DebugSpecial] 越界：(%d,%d) 地图 %dx%d"), GridLoc.X, GridLoc.Y, GridCountX, GridCountY);
		return;
	}

	const uint32 GridIndex = WorldMapData->GridLocationToGridIndex(GridLoc);
	const int32 CellIndex = static_cast<int32>(GridIndex);
	UE_LOG(LogFlowField, Log,
		TEXT("[DebugSpecial] cell(%d,%d) idx=%u base=%u building=%u hasSpecial=%d composed=%u costField=%u (special 条目=%d)"),
		GridLoc.X, GridLoc.Y, GridIndex,
		LayeredCostField->GetBaseCost(GridIndex),
		LayeredCostField->GetBuildingCost(GridIndex),
		LayeredCostField->HasSpecialContribution(GridIndex) ? 1 : 0,
		LayeredCostField->ComposeCellCost(GridIndex),
		WorldMapData->CostFields.IsValidIndex(CellIndex) ? WorldMapData->CostFields[CellIndex].Cost : 0,
		LayeredCostField->GetSpecialOpCount());
}

void AAsceFlowFieldDebugger::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 解绑选中监听
	if (SelectionSourceCommander.IsValid())
	{
		SelectionSourceCommander->OnSelectionChanged.Remove(OnSelectionChangedHandle);
	}
	SelectionSourceCommander = nullptr;
	OnSelectionChangedHandle.Reset();

	// 解绑Pawn绑定监听
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->OnPossessedPawnChanged.RemoveAll(this);
	}

	if (WorldMapData)
	{
		WorldMapData->OnCostFieldChanged.RemoveAll(this);
		WorldMapData->OnStructureChanged.RemoveAll(this);
	}

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && PC->InputComponent)
	{
		PC->InputComponent->ClearBindingsForObject(this);
	}
	Super::EndPlay(EndPlayReason);
}

void AAsceFlowFieldDebugger::OnCostFieldChanged(uint32 GridIndex)
{
	// 只收集脏格子，Tick末尾批量写像素——避免每格一次UpdateResource撑爆渲染资源
	if (bMapTextureDirty)
	{
		return; // 反正要全量重写
	}
	if (!DebugTexture || DebugTexture->GetSizeX() == 0)
	{
		bMapTextureDirty = true;
		return;
	}
	PendingTexturePixelUpdates.Add(GridIndex);
}

void AAsceFlowFieldDebugger::OnStructureChanged()
{
	// Portal/Island结构变化：全量重写纹理（Tick末尾统一处理，合并同一帧内的多次广播）
	bMapTextureDirty = true;
}

void AAsceFlowFieldDebugger::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// ReplicatorActor 可能晚于本Actor就绪：未绑定时重试，绑定前不做任何事
	if (!bMapBound)
	{
		TryBindMap();
	}
	if (!WorldMapData)
	{
		return;
	}

	// 按住 F/E 编辑鼠标处代价（支持拖动刷墙/擦墙）——仅权威端可编辑
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (bMapAuthority && PC && (PC->IsInputKeyDown(EKeys::F) || PC->IsInputKeyDown(EKeys::E)))
	{
		FHitResult Hit;
		if (PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit))
		{
			const FIntVector2 GridLoc = FWorldMapData::WorldLocationToGridLocation(FVector2D(Hit.Location.X, Hit.Location.Y));
			const uint32 GridIndex = WorldMapData->GridLocationToGridIndex(GridLoc);
			if (GridIndex != LastEditGridIndex)
			{
				LastEditGridIndex = GridIndex;
				if (PC->IsInputKeyDown(EKeys::F))
				{
					SetDebugTerrainCost(*WorldMapData, LayeredCostField, GridIndex, AsceCostField::BlockedCost);
				}
				else
				{
					SetDebugTerrainCost(*WorldMapData, LayeredCostField, GridIndex, AsceCostField::PlainPassableCost);
				}
			}
		}
	}

	// 世界数据时间片更新已挪到 `AAsceWorldMapReplicatorActor::Tick`（两端每帧驱动，debugger 只负责绘制/调试操作）

	// 每 2 秒清理一次无人引用的流场缓存
	if (GFrameCounter % 120 == 0 && PathFinder)
	{
		PathFinder->CleanupFlowFieldCache();
	}

	// 玩家移动超过一个格子且距上次重绘超过节流间隔才触发跟随重绘
	FVector2D PlayerPos;
	if (DebugDrawRange > 0.f && GetPlayerWorldPos(PlayerPos))
	{
		const float Now = GetWorld()->GetTimeSeconds();
		if (FVector2D::DistSquared(PlayerPos, LastRedrawPlayerPos) >= GRID_SIZE * GRID_SIZE
			&& Now - LastRedrawTime >= DebugRedrawInterval)
		{
			LastRedrawPlayerPos = PlayerPos;
			LastRedrawTime = Now;
			bNeedsRedraw = true;
		}
	}

	// 世界数据有变更或需要重绘时刷新
	if (bNeedsRedraw || WorldMapData->HasPendingWorldUpdates())
	{
		RedrawAll();
		bNeedsRedraw = false;
		LastRedrawTime = GetWorld()->GetTimeSeconds();
	}

	// 地图纹理（岛色+墙）只在数据变化时更新——decal固定在世界坐标，玩家移动无需重画
	if (bMapTextureDirty || WorldMapData->HasPendingWorldUpdates())
	{
		UpdateMapTexture();
		PendingTexturePixelUpdates.Reset();
		bMapTextureDirty = false;
	}
	else if (PendingTexturePixelUpdates.Num() > 0)
	{
		// 批量写脏格像素，一次UpdateResource
		FlushTexturePixelUpdates();
		PendingTexturePixelUpdates.Reset();
	}
}

void AAsceFlowFieldDebugger::OnRightMousePressed()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}

	FHitResult Hit;
	if (PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		const FVector2D GoalWorldPos(Hit.Location.X, Hit.Location.Y);
		IssueDebugPathRequest(GoalWorldPos);
	}
}

void AAsceFlowFieldDebugger::FlowFieldDebugPath(float WorldX, float WorldY)
{
	IssueDebugPathRequest(FVector2D(WorldX, WorldY));
}

void AAsceFlowFieldDebugger::IssueDebugPathRequest(const FVector2D& GoalWorldPos)
{
	if (!PathFinder)
	{
		return;
	}

	LastGoalWorldPosition = GoalWorldPos;

	// 同帧完成 A* + 流场（PortalChain 必须在同帧消费，避免 Portal 重排导致索引失效）
	LastPathResults.Reset();
	LastFlowFieldResults.Reset();
	if (LastPathPlanHandle.IsValid())
	{
		PathFinder->ReleasePathPlan(LastPathPlanHandle);
	}
	LastPathPlanHandle = PathFinder->RunMergeablePathRequest(GoalWorldPos, SourceWorldPositions, &LastPathResults);
	PathFinder->AcquirePathPlan(LastPathPlanHandle);

	bHasPathResult = false;
	if (SourceWorldPositions.Num() > 0)
	{
		const FIntVector2 GoalGrid = FWorldMapData::WorldLocationToGridLocation(GoalWorldPos);
		const FSector::FSectorID GoalSectorID = WorldMapData->GridLocationToSectorId(GoalGrid);

		for (const FPortalPathResult& Result : LastPathResults)
		{
			if (!Result.bSuccess)
			{
				continue;
			}

			bHasPathResult = true;

			TArray<TSharedPtr<FFlowFieldResult>> ChainFlowFields;
			PathFinder->RequestFlowFieldsForChain(
				Result.SourcePosition.SectorID.RawID,
				GoalSectorID.RawID,
				GoalGrid,
				Result.PortalChain,
				ChainFlowFields);

			for (const TSharedPtr<FFlowFieldResult>& FlowField : ChainFlowFields)
			{
				// 合并A*共享链后缀：同一(扇区,目标)流场命中缓存返回同一指针，去重避免重复绘制
				if (!LastFlowFieldResults.Contains(FlowField))
				{
					LastFlowFieldResults.Add(FlowField);
				}
			}
		}
	}

	bNeedsRedraw = true;
}

// ════════════════════════════════════════════════════════
// 绘制
// ════════════════════════════════════════════════════════

FVector AAsceFlowFieldDebugger::GridToWorld(const FIntVector2& GridLocation) const
{
	return FVector((GridLocation.X + 0.5f) * GRID_SIZE, (GridLocation.Y + 0.5f) * GRID_SIZE, DrawHeight);
}

bool AAsceFlowFieldDebugger::GetPlayerWorldPos(FVector2D& OutPos) const
{
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				const FVector Loc = Pawn->GetActorLocation();
				OutPos = FVector2D(Loc.X, Loc.Y);
				return true;
			}
		}
	}
	return false;
}

bool AAsceFlowFieldDebugger::IsInDrawRange(const FVector2D& WorldPos) const
{
	if (DebugDrawRange <= 0.f)
	{
		return true;
	}

	FVector2D PlayerPos;
	if (!GetPlayerWorldPos(PlayerPos))
	{
		return false; // 没有玩家就不画
	}

	return FVector2D::DistSquared(WorldPos, PlayerPos) <= DebugDrawRange * DebugDrawRange;
}

FVector2D AAsceFlowFieldDebugger::GetPortalRepresentativePos(const FPortal& Portal) const
{
	uint32 Bitmap = static_cast<uint32>(Portal.PortalBitMap);
	int32 MinBit = FMath::CountTrailingZeros(Bitmap);
	int32 MaxBit = 31 - FMath::CountLeadingZeros(Bitmap);
	int32 CenterPosAlongEdge = SECTOR_SIZE - 1 - (MinBit + MaxBit) / 2;

	FIntVector2 SectorLocation = WorldMapData->GetSectorLocation(Portal.OwingSectorID);
	switch (Portal.GetPortalType())
	{
	case FPortal::EPortalType::Left:
		return FVector2D(SectorLocation.X * GRID_SIZE, (SectorLocation.Y + CenterPosAlongEdge + 0.5f) * GRID_SIZE);
	case FPortal::EPortalType::Right:
		return FVector2D((SectorLocation.X + SECTOR_SIZE) * GRID_SIZE, (SectorLocation.Y + CenterPosAlongEdge + 0.5f) * GRID_SIZE);
	case FPortal::EPortalType::Down:
		return FVector2D((SectorLocation.X + CenterPosAlongEdge + 0.5f) * GRID_SIZE, SectorLocation.Y * GRID_SIZE);
	case FPortal::EPortalType::Up:
		return FVector2D((SectorLocation.X + CenterPosAlongEdge + 0.5f) * GRID_SIZE, (SectorLocation.Y + SECTOR_SIZE) * GRID_SIZE);
	default:
		return FVector2D::ZeroVector;
	}
}

void AAsceFlowFieldDebugger::RedrawAll()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FlushPersistentDebugLines(World);

	DrawPortals();
	if (bHasPathResult)
	{
		DrawPortalChain();
		DrawFlowFieldResults();
	}

	// Source / Goal 标记
	if (bDrawSourceGoalMarkers)
	{
		for (const FVector2D& SourcePos : SourceWorldPositions)
		{
			if (IsInDrawRange(SourcePos))
			{
				DrawDebugSphere(World, FVector(SourcePos.X, SourcePos.Y, DrawHeight + 40.f), 80.f, 12, FColor::Green, true, -1.f, 0, 3.f);
			}
		}
		if (bHasPathResult && IsInDrawRange(LastGoalWorldPosition))
		{
			DrawDebugSphere(World, FVector(LastGoalWorldPosition.X, LastGoalWorldPosition.Y, DrawHeight + 40.f), 80.f, 12, FColor::Red, true, -1.f, 0, 3.f);
		}
	}

	// 墙格簇线框（碰撞解算用的墙体AABB可视化）
	if (bDrawWallClusters)
	{
		DrawWallClusters();
	}
}

void AAsceFlowFieldDebugger::UpdateMapTexture()
{
	// 所有纹理层（扇区线/岛色/墙/Portal/LOS/积分）都关闭才隐藏
	if (!bDrawSectorGrid && !bDrawIslandFields && !bDrawWalls && !bDrawPortals && !bHasPathResult)
	{
		DebugDecal->SetVisibility(false);
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const int32 GridCountX = WorldMapData->GetGridCountX();
	const int32 GridCountY = WorldMapData->GetGridCountY();
	const int32 TexWidth  = GridCountX * CellPixelSize;
	const int32 TexHeight = GridCountY * CellPixelSize;

	// 1. 创建/复用 transient 纹理
	if (!DebugTexture || DebugTexture->GetSizeX() != TexWidth || DebugTexture->GetSizeY() != TexHeight)
	{
		DebugTexture = UTexture2D::CreateTransient(TexWidth, TexHeight, PF_B8G8R8A8);
	}
	if (!DebugTexture)
	{
		return;
	}

	FTexture2DMipMap& Mip = DebugTexture->GetPlatformData()->Mips[0];
	uint8* Pixels = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	FMemory::Memzero(Pixels, TexWidth * TexHeight * 4);

	// 岛屿颜色色板（暗色调），IslandId % 色板数
	static const FColor IslandColors[] = {
		FColor(160, 50, 50),   FColor(50, 160, 50),   FColor(50, 50, 160),
		FColor(160, 160, 50),  FColor(160, 50, 160),  FColor(50, 160, 160),
		FColor(160, 110, 50),  FColor(110, 50, 160),  FColor(110, 110, 110),
		FColor(170, 170, 170),
	};

	// 网格坐标 -> 纹理像素坐标（含UV调试开关）
	auto GridToTex = [&](int32 GridX, int32 GridY, int32& OutTexX, int32& OutTexY)
	{
		int32 U = bSwapUV ? GridY : GridX;
		int32 V = bSwapUV ? GridX : GridY;
		const int32 UMax = bSwapUV ? GridCountY - 1 : GridCountX - 1;
		const int32 VMax = bSwapUV ? GridCountX - 1 : GridCountY - 1;
		OutTexX = bFlipU ? UMax - U : U;
		OutTexY = bFlipV ? VMax - V : V;
	};

	// 2. 写像素：岛屿色块 + 墙（墙覆盖岛色）
	for (int32 SY = 0; SY < WorldMapData->GetSectorCountY(); ++SY)
	{
		for (int32 SX = 0; SX < WorldMapData->GetSectorCountX(); ++SX)
		{
			const FSector::FSectorID SectorID(SX + WorldMapData->GetSectorCountX() * SY);
			const FSector& Sector = WorldMapData->GetSectorData(SectorID);
			const FIntVector2 SectorLoc = WorldMapData->GetSectorLocation(SectorID);

			for (int32 i = 0; i < SECTOR_SIZE * SECTOR_SIZE; ++i)
			{
				const bool bWall = Sector.CostFields[i].IsWall();
				const uint8 IslandId = Sector.IslandFields[i].IslandId;
				if (!bWall && IslandId == 0)
				{
					continue;
				}

				const FIntVector2 Local(i % SECTOR_SIZE, i / SECTOR_SIZE);
				const int32 GridX = SectorLoc.X + Local.X;
				const int32 GridY = SectorLoc.Y + Local.Y;

				int32 TexX, TexY;
				GridToTex(GridX, GridY, TexX, TexY);

				const FColor CellColor = bWall
					? FColor(15, 15, 15)
					: IslandColors[(IslandId - 1) % UE_ARRAY_COUNT(IslandColors)];

				// 岛色块留1像素缝隙便于分辨格界；墙填满
				const int32 Inset = bWall ? 0 : 1;
				for (int32 py = Inset; py < CellPixelSize - Inset; ++py)
				{
					for (int32 px = Inset; px < CellPixelSize - Inset; ++px)
					{
						const int32 PixelIdx = ((TexY * CellPixelSize + py) * TexWidth + (TexX * CellPixelSize + px)) * 4;
						Pixels[PixelIdx + 0] = CellColor.B;
						Pixels[PixelIdx + 1] = CellColor.G;
						Pixels[PixelIdx + 2] = CellColor.R;
						Pixels[PixelIdx + 3] = 255;
					}
				}

				// 扇区边界线：格子的左/上边缘若落在扇区边界，涂深灰（画在缝隙像素上）
				if (bDrawSectorGrid)
				{
					if (GridX % SECTOR_SIZE == 0)
					{
						for (int32 py = 0; py < CellPixelSize; ++py)
						{
							const int32 PixelIdx = ((TexY * CellPixelSize + py) * TexWidth + TexX * CellPixelSize) * 4;
							Pixels[PixelIdx + 0] = 60;
							Pixels[PixelIdx + 1] = 60;
							Pixels[PixelIdx + 2] = 60;
							Pixels[PixelIdx + 3] = 255;
						}
					}
					if (GridY % SECTOR_SIZE == 0)
					{
						for (int32 px = 0; px < CellPixelSize; ++px)
						{
							const int32 PixelIdx = ((TexY * CellPixelSize) * TexWidth + TexX * CellPixelSize + px) * 4;
							Pixels[PixelIdx + 0] = 60;
							Pixels[PixelIdx + 1] = 60;
							Pixels[PixelIdx + 2] = 60;
							Pixels[PixelIdx + 3] = 255;
						}
					}
				}
			}
		}
	}

	// 3. Portal边界格画青色（覆盖岛色）
	for (int32 PortalIdx = 0; PortalIdx < WorldMapData->GetPortalCount(); ++PortalIdx)
	{
		const FPortal& Portal = WorldMapData->GetPortal(FPortal::FPortalIndex(PortalIdx));
		const FIntVector2 SectorLoc = WorldMapData->GetSectorLocation(Portal.OwingSectorID);

		for (int32 i = 0; i < SECTOR_SIZE; ++i)
		{
			const int32 Bit = SECTOR_SIZE - 1 - i;
			if ((Portal.PortalBitMap & (1 << Bit)) == 0)
			{
				continue;
			}

			FIntVector2 Local;
			switch (Portal.GetPortalType())
			{
			case FPortal::EPortalType::Left:  Local = FIntVector2(0, i);            break;
			case FPortal::EPortalType::Right: Local = FIntVector2(SECTOR_SIZE - 1, i); break;
			case FPortal::EPortalType::Down:  Local = FIntVector2(i, 0);            break;
			case FPortal::EPortalType::Up:    Local = FIntVector2(i, SECTOR_SIZE - 1); break;
			default: continue;
			}

			const int32 GridX = SectorLoc.X + Local.X;
			const int32 GridY = SectorLoc.Y + Local.Y;
			int32 TexX, TexY;
			GridToTex(GridX, GridY, TexX, TexY);

			for (int32 py = 0; py < CellPixelSize; ++py)
			{
				for (int32 px = 0; px < CellPixelSize; ++px)
				{
					const int32 PixelIdx = ((TexY * CellPixelSize + py) * TexWidth + (TexX * CellPixelSize + px)) * 4;
					Pixels[PixelIdx + 0] = 200;
					Pixels[PixelIdx + 1] = 200;
					Pixels[PixelIdx + 2] = 0;
					Pixels[PixelIdx + 3] = 255;
				}
			}
		}
	}

	// 4. LOS高亮 + 积分热力（路径结果，LOS覆盖积分，均留1像素缝）
	if (bHasPathResult && (bDrawLOS || bDrawIntegration))
	{
		for (const TSharedPtr<FFlowFieldResult>& Result : LastFlowFieldResults)
		{
			if (!Result.IsValid())
			{
				continue;
			}

			const FIntVector2 SectorLoc = WorldMapData->GetSectorLocation(Result->SectorID);

			uint16 MaxCost = 0;
			for (uint16 Cost : Result->IntegrationCosts)
			{
				if (Cost < MAX_uint16)
				{
					MaxCost = FMath::Max(MaxCost, Cost);
				}
			}
			if (MaxCost == 0)
			{
				MaxCost = 1;
			}

			for (int32 Y = 0; Y < SECTOR_SIZE; ++Y)
			{
				for (int32 X = 0; X < SECTOR_SIZE; ++X)
				{
					const int32 Index = X + Y * SECTOR_SIZE;
					const FFlowField& Field = Result->Directions[Index];

					FColor OverlayColor;
					bool bDraw = false;

					if (bDrawLOS && Field.HasLineOfSight())
					{
						OverlayColor = FColor(255, 220, 60);
						bDraw = true;
					}
					else if (bDrawIntegration)
					{
						const uint16 Cost = Result->IntegrationCosts[Index];
						if (Cost < MAX_uint16)
						{
							const float T = FMath::Clamp(static_cast<float>(Cost) / static_cast<float>(MaxCost), 0.f, 1.f);
							OverlayColor = FColor(
								static_cast<uint8>(FMath::Lerp(0.f, 255.f, T)),
								static_cast<uint8>(FMath::Lerp(255.f, 0.f, T)),
								40);
							bDraw = true;
						}
					}

					if (!bDraw)
					{
						continue;
					}

					const int32 GridX = SectorLoc.X + X;
					const int32 GridY = SectorLoc.Y + Y;
					int32 TexX, TexY;
					GridToTex(GridX, GridY, TexX, TexY);

					for (int32 py = 1; py < CellPixelSize - 1; ++py)
					{
						for (int32 px = 1; px < CellPixelSize - 1; ++px)
						{
							const int32 PixelIdx = ((TexY * CellPixelSize + py) * TexWidth + (TexX * CellPixelSize + px)) * 4;
							Pixels[PixelIdx + 0] = OverlayColor.B;
							Pixels[PixelIdx + 1] = OverlayColor.G;
							Pixels[PixelIdx + 2] = OverlayColor.R;
							Pixels[PixelIdx + 3] = 255;
						}
					}
				}
			}
		}
	}

	Mip.BulkData.Unlock();
	DebugTexture->UpdateResource();

	// 4. 设置 Decal 尺寸与位置（覆盖整张地图，固定在世界坐标，不随玩家移动）
	const float MapWidth  = GridCountX * GRID_SIZE;
	const float MapHeight = GridCountY * GRID_SIZE;
	DebugDecal->DecalSize = FVector(10000.f, MapWidth * 0.5f, MapHeight * 0.5f);
	DebugDecal->SetWorldLocation(FVector(MapWidth * 0.5f, MapHeight * 0.5f, DrawHeight));

	if (!DebugDecal->GetMaterial(0) && IsValid(DebugDecalMaterial))
	{
		DebugDecal->SetMaterial(0, DebugDecalMaterial);
	}
	if (!DebugMID && DebugDecal->GetMaterial(0))
	{
		DebugMID = DebugDecal->CreateDynamicMaterialInstance();
	}
	if (DebugMID)
	{
		DebugMID->SetTextureParameterValue(TEXT("DebugTexture"), DebugTexture);
	}
	DebugDecal->SetVisibility(true);
}

void AAsceFlowFieldDebugger::FlushTexturePixelUpdates()
{
	if (!DebugTexture || DebugTexture->GetSizeX() == 0 || PendingTexturePixelUpdates.Num() == 0)
	{
		return;
	}

	const int32 TexWidth = WorldMapData->GetGridCountX() * CellPixelSize;

	FTexture2DMipMap& Mip = DebugTexture->GetPlatformData()->Mips[0];
	uint8* Pixels = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));

	for (uint32 GridIndex : PendingTexturePixelUpdates)
	{
		WriteGridPixelToBuffer(Pixels, TexWidth, GridIndex);
	}

	Mip.BulkData.Unlock();
	DebugTexture->UpdateResource();
}

void AAsceFlowFieldDebugger::WriteGridPixelToBuffer(uint8* Pixels, int32 TexWidth, uint32 GridIndex)
{
	const int32 GridCountX = WorldMapData->GetGridCountX();
	const int32 GridCountY = WorldMapData->GetGridCountY();

	static const FColor IslandColors[] = {
		FColor(160, 50, 50),   FColor(50, 160, 50),   FColor(50, 50, 160),
		FColor(160, 160, 50),  FColor(160, 50, 160),  FColor(50, 160, 160),
		FColor(160, 110, 50),  FColor(110, 50, 160),  FColor(110, 110, 110),
		FColor(170, 170, 170),
	};

	const FIntVector2 GridLoc = WorldMapData->GridIndexToGridLocation(GridIndex);
	const FSector::FSectorID SectorID = WorldMapData->GridLocationToSectorId(GridLoc);
	if (!SectorID.IsValid())
	{
		return;
	}
	const FSector& Sector = WorldMapData->GetSectorData(SectorID);
	const uint32 LocalIndex = WorldMapData->GridLocationToGridIndexInSector(GridLoc);
	const bool bWall = Sector.CostFields[LocalIndex].IsWall();
	const uint8 IslandId = Sector.IslandFields[LocalIndex].IslandId;

	// 网格坐标 -> 纹理像素坐标（与UpdateMapTexture一致）
	int32 U = bSwapUV ? GridLoc.Y : GridLoc.X;
	int32 V = bSwapUV ? GridLoc.X : GridLoc.Y;
	const int32 UMax = bSwapUV ? GridCountY - 1 : GridCountX - 1;
	const int32 VMax = bSwapUV ? GridCountX - 1 : GridCountY - 1;
	const int32 TexX = bFlipU ? UMax - U : U;
	const int32 TexY = bFlipV ? VMax - V : V;

	FColor CellColor(0, 0, 0, 0);
	bool bDraw = false;
	if (bWall)
	{
		CellColor = FColor(15, 15, 15);
		bDraw = true;
	}
	else if (IslandId != 0)
	{
		CellColor = IslandColors[(IslandId - 1) % UE_ARRAY_COUNT(IslandColors)];
		bDraw = true;
	}

	const int32 Inset = bWall ? 0 : 1;
	for (int32 py = 0; py < CellPixelSize; ++py)
	{
		for (int32 px = 0; px < CellPixelSize; ++px)
		{
			const int32 PixelIdx = ((TexY * CellPixelSize + py) * TexWidth + (TexX * CellPixelSize + px)) * 4;
			if (bDraw && py >= Inset && py < CellPixelSize - Inset && px >= Inset && px < CellPixelSize - Inset)
			{
				Pixels[PixelIdx + 0] = CellColor.B;
				Pixels[PixelIdx + 1] = CellColor.G;
				Pixels[PixelIdx + 2] = CellColor.R;
				Pixels[PixelIdx + 3] = 255;
			}
			else
			{
				Pixels[PixelIdx + 0] = 0;
				Pixels[PixelIdx + 1] = 0;
				Pixels[PixelIdx + 2] = 0;
				Pixels[PixelIdx + 3] = 0;
			}
		}
	}
}

void AAsceFlowFieldDebugger::DrawPortals()
{
	if (!bDrawPortals)
	{
		return;
	}

	UWorld* World = GetWorld();

	// Portal格子已画在地图纹理里（青色），动态层只画代表点球和链接线
	for (int32 PortalIdx = 0; PortalIdx < WorldMapData->GetPortalCount(); ++PortalIdx)
	{
		const FPortal& Portal = WorldMapData->GetPortal(FPortal::FPortalIndex(PortalIdx));

		// 代表点不在玩家附近则跳过整个 Portal
		const FVector2D Rep = GetPortalRepresentativePos(Portal);
		if (!IsInDrawRange(Rep))
		{
			continue;
		}

		// 代表点 + 与对面 Portal 的连线
		DrawDebugSphere(World, FVector(Rep.X, Rep.Y, DrawHeight + 55.f), 30.f, 8, FColor::Cyan, true, -1.f, 0, 2.f);

		const FPortal& LinkPortal = Portal.GetLinkToPortal(*WorldMapData);
		if (LinkPortal.IsValid())
		{
			const FVector2D LinkRep = GetPortalRepresentativePos(LinkPortal);
			DrawDebugLine(World,
				FVector(Rep.X, Rep.Y, DrawHeight + 55.f),
				FVector(LinkRep.X, LinkRep.Y, DrawHeight + 55.f),
				FColor::Magenta, true, -1.f, 0, 2.f);
		}
	}
}

void AAsceFlowFieldDebugger::DrawPortalChain()
{
	if (!bDrawPortalChain)
	{
		return;
	}

	UWorld* World = GetWorld();

	// 每个source一条链（合并A*结果按source对应）
	for (const FPortalPathResult& PathResult : LastPathResults)
	{
		if (!PathResult.bSuccess)
		{
			continue;
		}

		const TArray<FPortal::FPortalIndex>& Chain = PathResult.PortalChain;

		if (Chain.Num() == 0)
		{
			continue; // 同扇区路径，无 Portal 链
		}

		// Source → 第一个 Portal
		const FPortal& FirstPortal = WorldMapData->GetPortal(Chain[0]);
		const FVector2D FirstRep = GetPortalRepresentativePos(FirstPortal);
		if (IsInDrawRange(FirstRep))
		{
			DrawDebugSphere(World, FVector(FirstRep.X, FirstRep.Y, DrawHeight + 40.f), 40.f, 8, FColor::Green, true, -1.f, 0, 2.f);
		}

		for (int32 i = 0; i + 1 < Chain.Num(); ++i)
		{
			const FVector2D A = GetPortalRepresentativePos(WorldMapData->GetPortal(Chain[i]));
			const FVector2D B = GetPortalRepresentativePos(WorldMapData->GetPortal(Chain[i + 1]));
			if (!IsInDrawRange(A) && !IsInDrawRange(B))
			{
				continue;
			}
			DrawDebugLine(World,
				FVector(A.X, A.Y, DrawHeight + 40.f),
				FVector(B.X, B.Y, DrawHeight + 40.f),
				FColor::Orange, true, -1.f, 0, 5.f);
		}

		// 最后一个 Portal → Goal
		const FPortal& LastPortal = WorldMapData->GetPortal(Chain.Last());
		const FVector2D LastRep = GetPortalRepresentativePos(LastPortal);
		if (IsInDrawRange(LastRep) || IsInDrawRange(LastGoalWorldPosition))
		{
			DrawDebugLine(World,
				FVector(LastRep.X, LastRep.Y, DrawHeight + 40.f),
				FVector(LastGoalWorldPosition.X, LastGoalWorldPosition.Y, DrawHeight + 40.f),
				FColor::Orange, true, -1.f, 0, 5.f);
		}
	}
}

void AAsceFlowFieldDebugger::DrawFlowFieldResults()
{
	UWorld* World = GetWorld();

	// 同一扇区可能有多条不同目标的流场（跨岛场景），按条目序号分配颜色便于区分
	static const FColor FlowFieldColors[] = {
		FColor::White, FColor::Red, FColor::Blue, FColor::Green,
		FColor::Yellow, FColor::Magenta, FColor::Cyan, FColor::Orange,
	};

	int32 ResultIndex = 0;
	for (const TSharedPtr<FFlowFieldResult>& Result : LastFlowFieldResults)
	{
		if (!Result.IsValid())
		{
			continue;
		}

		const FColor& ArrowColor = FlowFieldColors[ResultIndex % UE_ARRAY_COUNT(FlowFieldColors)];
		++ResultIndex;

		FIntVector2 SectorLoc = WorldMapData->GetSectorLocation(Result->SectorID);

		// 扇区中心上方打印该流场的目标信息（诊断同扇区多条流场时用）
		{
			FString TargetInfo;
			if (Result->Target.IsType<FPortal::FPortalIndex>())
			{
				const FPortal& TargetPortal = WorldMapData->GetPortal(Result->Target.Get<FPortal::FPortalIndex>());
				const FVector2D TargetRep = GetPortalRepresentativePos(TargetPortal);
				TargetInfo = FString::Printf(TEXT("FF%d: Sector(%d,%d) -> Portal#%d (%.0f,%.0f)"),
					ResultIndex,
					Result->SectorID.RawID % WorldMapData->GetSectorCountX(),
					Result->SectorID.RawID / WorldMapData->GetSectorCountX(),
					Result->Target.Get<FPortal::FPortalIndex>().RawIndex,
					TargetRep.X, TargetRep.Y);
			}
			else
			{
				const FIntVector2 GoalGrid = Result->Target.Get<FIntVector2>();
				TargetInfo = FString::Printf(TEXT("FF%d: Sector(%d,%d) -> GoalGrid(%d,%d)"),
					ResultIndex,
					Result->SectorID.RawID % WorldMapData->GetSectorCountX(),
					Result->SectorID.RawID / WorldMapData->GetSectorCountX(),
					GoalGrid.X, GoalGrid.Y);
			}
			const FVector LabelPos(
				(SectorLoc.X + SECTOR_SIZE * 0.5f) * GRID_SIZE,
				(SectorLoc.Y + SECTOR_SIZE) * GRID_SIZE + 60.f,
				DrawHeight + 150.f);
			if (IsInDrawRange(FVector2D(LabelPos.X, LabelPos.Y)))
			{
				DrawDebugString(World, LabelPos, TargetInfo, nullptr, ArrowColor, -1.f, false, 1.2f);
			}
		}

		// 扇区不在玩家附近则跳过
		{
			const FVector2D SectorCenter(
				(SectorLoc.X + SECTOR_SIZE * 0.5f) * GRID_SIZE,
				(SectorLoc.Y + SECTOR_SIZE * 0.5f) * GRID_SIZE);
			if (!IsInDrawRange(SectorCenter))
			{
				continue;
			}
		}

		// LOS高亮/积分热力已画在地图纹理里，动态层只画方向箭头
		for (int32 Y = 0; Y < SECTOR_SIZE; ++Y)
		{
			for (int32 X = 0; X < SECTOR_SIZE; ++X)
			{
				const int32 Index = X + Y * SECTOR_SIZE;
				const FFlowField& Field = Result->Directions[Index];
				const FVector Center(
					(SectorLoc.X + X + 0.5f) * GRID_SIZE,
					(SectorLoc.Y + Y + 0.5f) * GRID_SIZE,
					DrawHeight);

				if (!IsInDrawRange(FVector2D(Center.X, Center.Y)))
				{
					continue;
				}

				// 流场方向箭头
				if (bDrawFlowArrows && Field.Pathable())
				{
					const uint8 DirIdx = static_cast<uint8>(Field.GetDirection());
					const float AngleDeg = DirIdx * 22.5f;
					const FVector2D Dir(
						FMath::Cos(FMath::DegreesToRadians(AngleDeg)),
						FMath::Sin(FMath::DegreesToRadians(AngleDeg)));

					const FVector ArrowBase = Center + FVector(0.f, 0.f, 80.f);
					const FVector ArrowEnd = ArrowBase + FVector(Dir.X, Dir.Y, 0.f) * GRID_SIZE * 0.35f;
					DrawDebugLine(World, ArrowBase, ArrowEnd, ArrowColor, true, -1.f, 0, 4.f);

					// 简单箭头尖
					const FVector2D Perp(-Dir.Y, Dir.X);
					const FVector Tip = ArrowEnd;
					const FVector LeftWing  = Tip - FVector(Dir.X, Dir.Y, 0.f) * 20.f + FVector(Perp.X, Perp.Y, 0.f) * 12.f;
					const FVector RightWing = Tip - FVector(Dir.X, Dir.Y, 0.f) * 20.f - FVector(Perp.X, Perp.Y, 0.f) * 12.f;
					DrawDebugLine(World, Tip, LeftWing, ArrowColor, true, -1.f, 0, 3.f);
					DrawDebugLine(World, Tip, RightWing, ArrowColor, true, -1.f, 0, 3.f);
				}
			}
		}
	}
}

void AAsceFlowFieldDebugger::DrawWallClusters()
{
	// 墙格簇：碰撞解算用的墙体AABB，画黄色线框可视化便于核查簇的范围/是否漏格。
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FColor ClusterColor(255, 255, 0); // 黄

	for (const FWallCluster& Cluster : WorldMapData->WallClusters)
	{
		const FVector2D Center((Cluster.Min.X + Cluster.Max.X) * 0.5f, (Cluster.Min.Y + Cluster.Max.Y) * 0.5f);
		if (!IsInDrawRange(Center))
		{
			continue;
		}

		// AABB线框（地面高度DrawHeight）
		const float Z = DrawHeight + 20.f;
		const FVector P00(Cluster.Min.X, Cluster.Min.Y, Z);
		const FVector P10(Cluster.Max.X, Cluster.Min.Y, Z);
		const FVector P11(Cluster.Max.X, Cluster.Max.Y, Z);
		const FVector P01(Cluster.Min.X, Cluster.Max.Y, Z);
		DrawDebugLine(World, P00, P10, ClusterColor, true, -1.f, 0, 3.f);
		DrawDebugLine(World, P10, P11, ClusterColor, true, -1.f, 0, 3.f);
		DrawDebugLine(World, P11, P01, ClusterColor, true, -1.f, 0, 3.f);
		DrawDebugLine(World, P01, P00, ClusterColor, true, -1.f, 0, 3.f);
	}
}

#endif
