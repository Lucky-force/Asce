// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceFogSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Algo/BinarySearch.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformAtomics.h"
#include "HAL/PlatformMisc.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManagerStorage.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "AsceFogProcessor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceFogSubsystem)

/** 视野半径计算上限（防御异常配置；窗口/位图解析必须与 ComputeVisionForCell 的 clamp 保持一致）。 */
static constexpr int32 GMaxComputeRadius = 512;

// ── lifecycle ──

UAsceFogSubsystem::UAsceFogSubsystem()
{
}

bool UAsceFogSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// 只在 GameWorld / PIE 创建，不在编辑器预览世界创建（与 UAsceSpatialGridSubsystem 一致）
	UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	return World->IsGameWorld() || World->IsPlayInEditor();
}

void UAsceFogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAsceFogSubsystem::Deinitialize()
{
	FogProcessor = nullptr;
	ActiveResults.Reset();
	OcclusionDirty.Reset();
	Super::Deinitialize();
}

void UAsceFogSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 手动创建并驱动 FogProcessor（与 UAsceSpatialGridSubsystem 相同的模式）：
	// Mass 处理器不在关卡全局调度里跑，而是本子系统在 Tick 里手动 UE::Mass::Executor::Run。
	// 注意：Initialize 用并发存储参数，避免旧的单线程版本 deprecated 警告。
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}
	if (UMassEntitySubsystem* EntitySubsys = World->GetSubsystem<UMassEntitySubsystem>())
	{
		FMassEntityManager& EntityManager = EntitySubsys->GetMutableEntityManager();
		FMassEntityManagerStorageInitParams InitParams;
		InitParams.Emplace<FMassEntityManager_InitParams_Concurrent>();
		EntityManager.Initialize(InitParams);
		FogProcessor = NewObject<UAsceFogProcessor>(this);
		FogProcessor->CallInitialize(this, EntityManager.AsShared());
	}
}

void UAsceFogSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	FogProcessor = nullptr;
	ActiveResults.Reset();
	OcclusionDirty.Reset();
	Super::OnWorldEndPlay(InWorld);
}

void UAsceFogSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!FogProcessor)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}
	if (UMassEntitySubsystem* EntitySubsys = World->GetSubsystem<UMassEntitySubsystem>())
	{
		FMassEntityManager& EntityManager = EntitySubsys->GetMutableEntityManager();
		FMassProcessingContext ProcessingContext(EntityManager, DeltaTime);
		UE::Mass::Executor::Run(*FogProcessor, ProcessingContext);
	}
}

TStatId UAsceFogSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAsceFogSubsystem, STATGROUP_Tickables);
}

UAsceFogSubsystem* UAsceFogSubsystem::Get(const UObject* WorldContextObject)
{
	if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		return UWorld::GetSubsystem<UAsceFogSubsystem>(World);
	}
	return nullptr;
}

void UAsceFogSubsystem::InitializeVisionMap(int32 GridW, int32 GridH, float VisionCellSizeCm)
{
	HeightMap.Init(GridW, GridH, VisionCellSizeCm);

	// 重新初始化场景：清掉所有派生状态并按格重建（FGridInfo 含不可拷贝的 FCriticalSection：
	// 先 Reserve 足量再逐格构造，避免 TArray 搬移）
	ActiveResults.Reset();
	OcclusionDirty.Reset();

	const int32 GridCount = HeightMap.GetGridCount();
	GridInfos.Reset();
	GridInfos.Reserve(GridCount);
	for (int32 i = 0; i < GridCount; ++i)
	{
		GridInfos.AddDefaulted();
	}

	// 投影固定分配 [MaxTeamCount]，避免运行时扩容（并发写投影/查询不再需要结构锁）
	const int32 WordCount = (GridCount + 31) / 32;
	TeamProjections.Reset();
	TeamProjections.SetNum(AsceVision::MaxTeamCount, EAllowShrinking::No);
	for (int32 i = 0; i < AsceVision::MaxTeamCount; ++i)
	{
		TeamProjections[i].SetNumZeroed(WordCount);
	}

	// 脏 tile 位图：2D 方块（64×64 格）
	TilesX = (HeightMap.GetGridW() + TileCells - 1) / TileCells;
	TilesY = (HeightMap.GetGridH() + TileCells - 1) / TileCells;
	const int32 DirtyWordsPerTeam = (TilesX * TilesY + 31) / 32;
	TeamDirtyTileWords.Reset();
	TeamDirtyTileWords.SetNum(AsceVision::MaxTeamCount, EAllowShrinking::No);
	for (int32 i = 0; i < AsceVision::MaxTeamCount; ++i)
	{
		TeamDirtyTileWords[i].SetNumZeroed(DirtyWordsPerTeam);
	}

	// 档变更分片：取系统 worker 线程数的稳定上界（并发写者 ≤ worker 数）
	const int32 NumShards = FMath::Max(4, FPlatformMisc::NumberOfWorkerThreadsToSpawn());
	PendingChangeShards.SetNum(NumShards, EAllowShrinking::No);
	for (FChangeOpShard& Shard : PendingChangeShards)
	{
		Shard.Lock = 0;
		Shard.Items.Reset();
	}

	UE_LOG(LogVision, Log, TEXT("UAsceFogSubsystem 初始化视野地图 %dx%d (%.1fcm/格)，变更分片=%d"),
		GridW, GridH, VisionCellSizeCm, NumShards);
}

uint16 UAsceFogSubsystem::ComputeHe(uint16 SourceCellEffectiveHeight, uint16 EyeHeightCm)
{
	return static_cast<uint16>(FMath::Min<uint32>(static_cast<uint32>(SourceCellEffectiveHeight) + EyeHeightCm, MAX_uint16));
}

// ── flood 核心（阈值 + 墙模型） ──

namespace
{
	struct FWindowBits
	{
		TArray<uint32>& Words;

		explicit FWindowBits(TArray<uint32>& InWords) : Words(InWords) {}

		FORCEINLINE void Set(int32 Idx)
		{
			Words[Idx >> 5] |= (1u << (Idx & 31));
		}
		FORCEINLINE bool Test(int32 Idx) const
		{
			return (Words[Idx >> 5] & (1u << (Idx & 31))) != 0;
		}
	};
}

void UAsceFogSubsystem::ComputeVisionForCell(const FAsceVisionHeightMap& HMap, const FIntVector2& S, uint16 He, uint16 R, TArray<uint32>& OutBits)
{
	const int32 RClamped = FMath::Min<int32>(R, GMaxComputeRadius); // 防御异常配置（视野半径应几十格量级）
	if (RClamped != R)
	{
		UE_LOG(LogVision, Warning, TEXT("ComputeVisionForCell：R=%u 超过防御上限 %d，按上限计算"), R, GMaxComputeRadius);
	}

	const int32 Window = 2 * RClamped + 1;
	const int32 TotalBits = Window * Window;
	OutBits.SetNumZeroed((TotalBits + 31) / 32);

	if (!HMap.IsValid())
	{
		return; // 空高度场 → 全不可见
	}

	FWindowBits Bits(OutBits);

	const int32 RadiusSq = RClamped * RClamped;
	const int32 CenterIndex = RClamped * Window + RClamped; // 窗口内 (R, R)

	// visited 语义：置位 = 该格已判定且可见（墙格也可见，见 §4）。非墙格才入队继续扩散。
	Bits.Set(CenterIndex);

	TArray<int32> Queue;
	Queue.Reserve(TotalBits);
	Queue.Add(CenterIndex);

	int32 Head = 0;
	while (Head < Queue.Num())
	{
		const int32 Cur = Queue[Head++];
		const int32 CurX = Cur % Window;
		const int32 CurY = Cur / Window;

		auto TryNeighbor = [&](int32 NX, int32 NY)
		{
			if (NX < 0 || NX >= Window || NY < 0 || NY >= Window)
			{
				return;
			}
			const int32 OffX = NX - RClamped;
			const int32 OffY = NY - RClamped;
			if (OffX * OffX + OffY * OffY > RadiusSq)
			{
				return; // 圆外：不可达（有意近似，见 §4"范围界定"）
			}
			const int32 NIndex = NY * Window + NX;
			if (Bits.Test(NIndex))
			{
				return; // 已判定
			}

			// 图外不置位、不扩散
			const FIntVector2 Global(S.X + OffX, S.Y + OffY);
			if (!HMap.IsInside(Global))
			{
				return;
			}

			// 标记可见（墙格自身可见），再判断是否可透传
			Bits.Set(NIndex);
			const uint32 EffectiveH = HMap.GetEffectiveHeight(HMap.CellToIndex(Global));
			const bool bWall = EffectiveH > static_cast<uint32>(He);
			if (!bWall)
			{
				Queue.Add(NIndex);
			}
		};

		TryNeighbor(CurX - 1, CurY);
		TryNeighbor(CurX + 1, CurY);
		TryNeighbor(CurX, CurY - 1);
		TryNeighbor(CurX, CurY + 1);
	}
}

// ── per-team 可见位图投影（InitializeVisionMap 固定分配 [MaxTeamCount]，无运行时扩容） ──

const TArray<uint32>* UAsceFogSubsystem::GetTeamVisibleBits(int32 TeamKey) const
{
	const int32 Index = GetIndex(TeamKey);
	if (Index < 0 || Index >= TeamProjections.Num())
	{
		return nullptr;
	}
	return &TeamProjections[Index];
}

// ── 脏 tile（2D 方块；GT 单线程，与 ApplyBitsToTeamCounts 同线程） ──

void UAsceFogSubsystem::MarkTileDirty(int32 TeamIndex, int32 TileIndex)
{
	if (TeamIndex < 0 || TeamIndex >= TeamDirtyTileWords.Num()
		|| TileIndex < 0 || TileIndex >= TilesX * TilesY)
	{
		return;
	}
	TArray<uint32>& DirtyWords = TeamDirtyTileWords[TeamIndex];
	const int32 WordIdx = TileIndex >> 5;
	const uint32 Mask = 1u << (TileIndex & 31);
	// 单线程：先读判断，已脏则跳过写（省重复写/缓存行争用）
	if ((DirtyWords[WordIdx] & Mask) == 0)
	{
		DirtyWords[WordIdx] |= Mask;
	}
}

void UAsceFogSubsystem::DrainDirtyTiles(int32 TeamKey, TArray<int32>& OutTileIndices)
{
	OutTileIndices.Reset();

	const int32 TeamIndex = GetIndex(TeamKey);
	if (TeamIndex < 0 || TeamIndex >= TeamDirtyTileWords.Num())
	{
		return;
	}

	TArray<uint32>& DirtyWords = TeamDirtyTileWords[TeamIndex];
	for (int32 WordIdx = 0; WordIdx < DirtyWords.Num(); ++WordIdx)
	{
		uint32 Word = DirtyWords[WordIdx];
		if (Word == 0)
		{
			continue;
		}
		DirtyWords[WordIdx] = 0;
		while (Word != 0)
		{
			const uint32 Bit = FMath::CountTrailingZeros(Word);
			Word &= Word - 1;
			OutTileIndices.Add(WordIdx * 32 + static_cast<int32>(Bit));
		}
	}
}

void UAsceFogSubsystem::ExtractTileWords(int32 TeamKey, int32 TileIndex, TArray<uint32>& OutWords) const
{
	OutWords.Reset();

	const int32 TeamIndex = GetIndex(TeamKey);
	if (TeamIndex < 0 || TeamIndex >= TeamProjections.Num() || TileIndex < 0
		|| TileIndex >= TilesX * TilesY || !HeightMap.IsValid())
	{
		OutWords.SetNumZeroed(TileWords);
		return;
	}

	const int32 GridW = HeightMap.GetGridW();
	const int32 GridH = HeightMap.GetGridH();
	// 初始化已保证视野网格是 TileCells 的整数倍（且 GridW%32==0）：tile 完整、每行字对齐 → 直接按行 memcpy。
	checkf((GridW % TileCells) == 0 && (GridH % TileCells) == 0,
		TEXT("ExtractTileWords 依赖视野网格为 TileCells(%d) 的整数倍（由地图初始化对齐保证）"), TileCells);

	const TArray<uint32>& Projection = TeamProjections[TeamIndex];
	const int32 WordsPerRow = GridW >> 5;

	const int32 TileX = TileIndex % TilesX;
	const int32 TileY = TileIndex / TilesX;
	// 该 tile 首行首字的源指针（平坦打包下：行 stride = WordsPerRow 个字）
	const uint32* SrcBase = Projection.GetData()
		+ (TileY * TileCells) * WordsPerRow
		+ TileX * TileWordsPerRow;

	OutWords.SetNumUninitialized(TileWords);
	for (int32 Row = 0; Row < TileCells; ++Row)
	{
		FMemory::Memcpy(
			OutWords.GetData() + Row * TileWordsPerRow,
			SrcBase + Row * WordsPerRow,
			TileWordsPerRow * sizeof(uint32));
	}
}

void UAsceFogSubsystem::MarkAllTilesDirty(int32 TeamKey)
{
	const int32 TeamIndex = GetIndex(TeamKey);
	if (TeamIndex < 0 || TeamIndex >= TeamDirtyTileWords.Num())
	{
		return;
	}
	TArray<uint32>& DirtyWords = TeamDirtyTileWords[TeamIndex];
	const int32 NumTiles = TilesX * TilesY;
	for (int32 TileIndex = 0; TileIndex < NumTiles; ++TileIndex)
	{
		DirtyWords[TileIndex >> 5] |= (1u << (TileIndex & 31));
	}
}

// ── 单位视野贡献（源结果存格内 SourceParms；Occupants 决定上下线） ──

namespace
{
	/** (He,R) → SourceParms 的组合键 */
	FORCEINLINE uint64 ParmsKey(uint16 He, uint16 R)
	{
		return (static_cast<uint64>(He) << 16) | R;
	}
}

void UAsceFogSubsystem::AddContribution(int32 TeamKey, int32 CellIndex, uint16 He, uint16 R)
{
	if (!HeightMap.IsValid() || CellIndex < 0 || !HeightMap.IsInside(HeightMap.IndexToCell(CellIndex)))
	{
		return; // 地图外：无贡献
	}

	const int32 TeamIndex = GetIndex(TeamKey);
	if (TeamIndex < 0 || TeamIndex >= AsceVision::MaxTeamCount)
	{
		return;
	}

	const uint64 Packed = ParmsKey(He, R);
	const FActiveKey Key{ TeamKey, CellIndex, He, R };

	{
		FGridInfo& Cell = GridInfos[CellIndex];
		FScopeLock CellLock(&Cell.Lock);

		FTeamCellData& Slot = Cell.EnsureTeamSlot(TeamIndex);
		FGridResult* Existing = Slot.SourceParms.Find(Packed);
		if (Existing)
		{
			++Existing->Occupants;
			return; // 已有档：仅占位 +1（RAII 释放锁）
		}

		// 首次出现此档：在源格锁内建条目并算好结果（只读高度场、写本格 Bits，格间并行互不干扰）
		FGridResult& NewResult = Slot.SourceParms.Add(Packed, FGridResult{});
		NewResult.Occupants = 1;
		ComputeVisionForCell(HeightMap, HeightMap.IndexToCell(CellIndex), He, R, NewResult.Bits);

		// 可见性/ActiveResults 的更新延后到帧末单线程（ProcessPendingChanges 按最终 Occupants 判定）
		PushPendingChange(Key, false, TArray<uint32>());
	}
}

void UAsceFogSubsystem::RemoveContribution(int32 TeamKey, int32 CellIndex, uint16 He, uint16 R)
{
	const int32 TeamIndex = GetIndex(TeamKey);
	if (TeamIndex < 0 || TeamIndex >= AsceVision::MaxTeamCount || !HeightMap.IsValid())
	{
		return;
	}
	if (CellIndex < 0 || !HeightMap.IsInside(HeightMap.IndexToCell(CellIndex)))
	{
		return; // 越界/无效格防御（与 AddContribution 一致）
	}

	const uint64 Packed = ParmsKey(He, R);
	const FActiveKey Key{ TeamKey, CellIndex, He, R };

	{
		FGridInfo& Cell = GridInfos[CellIndex];
		FScopeLock CellLock(&Cell.Lock);

		FTeamCellData* Slot = Cell.FindTeamSlot(TeamIndex);
		if (!Slot)
		{
			return;
		}

		FGridResult* Result = Slot->SourceParms.Find(Packed);
		if (!Result || Result->Occupants <= 0)
		{
			return; // 未登记（含界外/无效 cell / 已清空）
		}

		if (Result->Occupants == 1)
		{
			// 最后一个此档单位离开：锁内删条目；把旧位图拷出，帧末单线程做撤销
			TArray<uint32> OldBits = Result->Bits;
			Slot->SourceParms.Remove(Packed);
			PushPendingChange(Key, true, MoveTemp(OldBits));
		}
		else
		{
			--Result->Occupants;
		}
	}
}

void UAsceFogSubsystem::PushPendingChange(const FActiveKey& Key, bool bRemove, TArray<uint32>&& RemoveBits)
{
	if (PendingChangeShards.Num() == 0)
	{
		return; // 未初始化（理论不会：InitializeVisionMap 已建分片）
	}

	// 试锁找空闲分片；全忙则回退阻塞第一片
	FChangeOpShard* Target = nullptr;
	for (FChangeOpShard& Shard : PendingChangeShards)
	{
		if (FPlatformAtomics::InterlockedCompareExchange(&Shard.Lock, 1, 0) == 0)
		{
			Target = &Shard;
			break;
		}
	}
	if (!Target)
	{
		Target = &PendingChangeShards[0];
		while (FPlatformAtomics::InterlockedCompareExchange(&Target->Lock, 1, 0) != 0)
		{
		}
	}

	FChangeOp& Op = Target->Items.AddDefaulted_GetRef();
	Op.Key = Key;
	Op.bRemove = bRemove;
	Op.RemoveBits = MoveTemp(RemoveBits);
	FPlatformAtomics::InterlockedExchange(&Target->Lock, 0);
}

void UAsceFogSubsystem::ProcessPendingChanges()
{
	// drain 全部变更分片（并行阶段已结束：本函数在 GT、无并发写者）
	TArray<FChangeOp> Ops;
	for (FChangeOpShard& Shard : PendingChangeShards)
	{
		while (FPlatformAtomics::InterlockedCompareExchange(&Shard.Lock, 1, 0) != 0)
		{
		}
		if (Shard.Items.Num() > 0)
		{
			Ops.Append(MoveTemp(Shard.Items));
			Shard.Items.Reset();
		}
		FPlatformAtomics::InterlockedExchange(&Shard.Lock, 0);
	}

	// 按"档的最终存在性"处理：每步都重新核对 Occupants，与分片顺序无关
	for (const FChangeOp& Op : Ops)
	{
		const int32 TeamIndex = GetIndex(Op.Key.TeamKey);
		if (TeamIndex < 0 || TeamIndex >= AsceVision::MaxTeamCount)
		{
			continue;
		}

		const FTeamCellData* Slot = GridInfos[Op.Key.CellIndex].FindTeamSlot(TeamIndex);
		const FGridResult* Result = Slot ? Slot->SourceParms.Find(ParmsKey(Op.Key.He, Op.Key.R)) : nullptr;
		const bool bOccupied = (Result && Result->Occupants > 0);

		const int32 Pos = Algo::LowerBound(ActiveResults, Op.Key);
		const bool bActive = (Pos < ActiveResults.Num() && ActiveResults[Pos] == Op.Key);

		if (Op.bRemove)
		{
			// 删除：仅当档已真正消失（无人占）才撤销可见性；若档被同帧重建则保留给后续 add 变更处理
			if (!bOccupied)
			{
				if (bActive)
				{
					ActiveResults.RemoveAt(Pos, 1, EAllowShrinking::No);
				}
				ApplyBitsToTeamCounts(Op.Key.TeamKey, HeightMap.IndexToCell(Op.Key.CellIndex), Op.Key.R, Op.RemoveBits, -1);
			}
		}
		else
		{
			// 新增：仅在"该档尚未登记（!bActive）"时做 登记+apply（同一 key 同帧多次 add 变更只生效一次，
			// 避免重复 +1 造成目标格计数残留；若 active 仍残留（同帧旧档的 remove 尚未处理）则其 +1 等价覆盖）
			if (bOccupied && !bActive)
			{
				ActiveResults.Insert(Op.Key, Pos);
				ApplyBitsToTeamCounts(Op.Key.TeamKey, HeightMap.IndexToCell(Op.Key.CellIndex), Op.Key.R, Result->Bits, +1);
			}
		}
	}
}

// ── 动态遮挡增删与翻转（Observer 事件驱动，局部，无整层重建） ──

void UAsceFogSubsystem::MarkOcclusionDirtyRect(int32 X0, int32 Y0, int32 X1, int32 Y1)
{
	if (!HeightMap.IsValid())
	{
		return;
	}

	const int32 GridW = HeightMap.GetGridW();
	const int32 GridH = HeightMap.GetGridH();
	const int32 ClampX0 = FMath::Max(0, X0);
	const int32 ClampY0 = FMath::Max(0, Y0);
	const int32 ClampX1 = FMath::Min(GridW - 1, X1);
	const int32 ClampY1 = FMath::Min(GridH - 1, Y1);
	if (ClampX0 > ClampX1 || ClampY0 > ClampY1)
	{
		return;
	}

	// OcclusionDirty 始终按 GridIndex 升序、去重（有序数组便于结果侧二分定位命中段）。
	// 矩形本身在行主序下是逐行递增的区间，先收集本矩形所有格再插入比逐格二分更省。
	for (int32 Y = ClampY0; Y <= ClampY1; ++Y)
	{
		const int32 RowBase = Y * GridW;
		for (int32 X = ClampX0; X <= ClampX1; ++X)
		{
			const int32 Index = RowBase + X;
			const int32 Pos = Algo::LowerBound(OcclusionDirty, Index);
			if (Pos < OcclusionDirty.Num() && OcclusionDirty[Pos] == Index)
			{
				continue; // 已存在（去重）
			}
			OcclusionDirty.Insert(Index, Pos);
		}
	}
}

void UAsceFogSubsystem::AddOcclusionRect(const FAsceOcclusionRangeWrite& Write)
{
	if (!HeightMap.IsValid())
	{
		return;
	}

	HeightMap.SetDynamicOcclusionRange(Write.X0, Write.Y0, Write.X1, Write.Y1, Write.OcclusionHeightCm);
	MarkOcclusionDirtyRect(Write.X0, Write.Y0, Write.X1, Write.Y1);
}

void UAsceFogSubsystem::RemoveOcclusionRect(const FAsceOcclusionRangeWrite& Rect)
{
	if (!HeightMap.IsValid())
	{
		return;
	}

	HeightMap.ClearDynamicOcclusionRect(Rect.X0, Rect.Y0, Rect.X1, Rect.Y1);
	MarkOcclusionDirtyRect(Rect.X0, Rect.Y0, Rect.X1, Rect.Y1);
}

void UAsceFogSubsystem::ProcessHeightChanges()
{
	// 先处理并行阶段塞入分片的档变更：按最终 Occupants 更新 ActiveResults 与可见 ±apply（单线程）
	ProcessPendingChanges();

	if (OcclusionDirty.Num() == 0 || !HeightMap.IsValid())
	{
		return; // 没有遮挡变化：零成本
	}

	const int32 GridW = HeightMap.GetGridW();
	const int32 GridH = HeightMap.GetGridH();

	// 对每个活跃源档：受遮挡影响的脏格必落在其"外接矩形"的 GridIndex 区间内。
	// OcclusionDirty 按 GridIndex 升序，二分定位起点后线性扫该小段即可；
	// 段内每格仍做坐标级圆精测（行主序区间是"外接整行带"，含矩形 X 范围外的整行格）。
	for (const FActiveKey& Key : ActiveResults)
	{
		const int32 TeamIndex = GetIndex(Key.TeamKey);
		if (TeamIndex < 0 || TeamIndex >= AsceVision::MaxTeamCount)
		{
			continue;
		}
		FTeamCellData* Slot = GridInfos[Key.CellIndex].FindTeamSlot(TeamIndex);
		if (!Slot)
		{
			continue;
		}
		FGridResult* Entry = Slot->SourceParms.Find(ParmsKey(Key.He, Key.R));
		if (!Entry)
		{
			continue;
		}

		const int32 RLocal = FMath::Min<int32>(Key.R, GMaxComputeRadius);
		const int32 RadiusSq = RLocal * RLocal;
		const FIntVector2 Center = HeightMap.IndexToCell(Key.CellIndex);

		// 圆外接矩形（截断到图内）对应的 GridIndex 闭区间
		const int32 X0 = FMath::Max(0, Center.X - RLocal);
		const int32 X1 = FMath::Min(GridW - 1, Center.X + RLocal);
		const int32 Y0 = FMath::Max(0, Center.Y - RLocal);
		const int32 Y1 = FMath::Min(GridH - 1, Center.Y + RLocal);
		const int32 IndexLo = Y0 * GridW + X0;
		const int32 IndexHi = Y1 * GridW + X1;

		bool bHit = false;
		const int32 StartPos = Algo::LowerBound(OcclusionDirty, IndexLo);
		for (int32 Pos = StartPos; Pos < OcclusionDirty.Num() && OcclusionDirty[Pos] <= IndexHi; ++Pos)
		{
			const int32 DirtyIndex = OcclusionDirty[Pos];
			const int32 DX = (DirtyIndex % GridW) - Center.X;
			const int32 DY = (DirtyIndex / GridW) - Center.Y;
			if (DX * DX + DY * DY <= RadiusSq)
			{
				bHit = true;
				break;
			}
		}
		if (!bHit)
		{
			continue;
		}

		// 快照旧位图 → 锁外 -1 → 短锁内用最新高度场重算 → 锁外 +1（不嵌套持源格锁）
		TArray<uint32> OldBits;
		{
			FScopeLock CellLock(&GridInfos[Key.CellIndex].Lock);
			OldBits = Entry->Bits;
		}
		ApplyBitsToTeamCounts(Key.TeamKey, Center, Key.R, OldBits, -1);

		{
			FScopeLock CellLock(&GridInfos[Key.CellIndex].Lock);
			ComputeVisionForCell(HeightMap, Center, Key.He, Key.R, Entry->Bits);
		}
		ApplyBitsToTeamCounts(Key.TeamKey, Center, Key.R, Entry->Bits, +1);
	}

	OcclusionDirty.Reset();
}

// ── 位图应用（计数图 + 派生可见位图同步） ──

void UAsceFogSubsystem::ApplyBitsToTeamCounts(int32 TeamKey, const FIntVector2& Center, uint16 R, const TArray<uint32>& Bits, int32 Sign)
{
	const int32 TeamIndex = GetIndex(TeamKey);
	if (TeamIndex < 0 || TeamIndex >= TeamProjections.Num() || !HeightMap.IsValid())
	{
		return; // 投影已在 InitializeVisionMap 固定分配
	}

	TArray<uint32>& Projection = TeamProjections[TeamIndex];

	const int32 RLocal = FMath::Min<int32>(R, GMaxComputeRadius);
	const int32 Window = 2 * RLocal + 1;
	const int32 GridW = HeightMap.GetGridW();
	const int32 GridH = HeightMap.GetGridH();
	const bool bIsAdd = (Sign >= 0);

	for (int32 WordIndex = 0; WordIndex < Bits.Num(); ++WordIndex)
	{
		uint32 Word = Bits[WordIndex];
		while (Word != 0)
		{
			const uint32 BitOffset = FMath::CountTrailingZeros(Word);
			Word &= Word - 1;

			const int32 BitIndex = WordIndex * 32 + static_cast<int32>(BitOffset);
			const int32 OffX = (BitIndex % Window) - RLocal;
			const int32 OffY = (BitIndex / Window) - RLocal;

			const int32 GX = Center.X + OffX;
			const int32 GY = Center.Y + OffY;
			if (GX < 0 || GX >= GridW || GY < 0 || GY >= GridH)
			{
				continue;
			}

			const int32 GlobalIndex = GX + GY * GridW;
			const uint32 Mask = 1u << (GlobalIndex & 31);
			uint32* WordPtr = &Projection[GlobalIndex >> 5];

			// 目标格计数：本格锁内改（0↔1 判定串行）；投影 word 用原子位写（跨格共享同一 word）
			FGridInfo& Info = GridInfos[GlobalIndex];
			bool bBitFlipped = false;
			{
				FScopeLock CellLock(&Info.Lock);
				FTeamCellData& Slot = Info.EnsureTeamSlot(TeamIndex);
				uint16& Count = Slot.VisibilityCount;
				if (bIsAdd)
				{
					const bool bWasInvisible = (Count == 0);
					if (Count < MAX_uint16)
					{
						++Count;
					}
					if (bWasInvisible)
					{
						FPlatformAtomics::InterlockedOr(reinterpret_cast<volatile int32*>(WordPtr), static_cast<int32>(Mask));
						bBitFlipped = true;
					}
				}
				else
				{
					if (Count > 0)
					{
						--Count;
					}
					if (Count == 0)
					{
						FPlatformAtomics::InterlockedAnd(reinterpret_cast<volatile int32*>(WordPtr), static_cast<int32>(~Mask));
						bBitFlipped = true;
					}
				}
			}
			if (bBitFlipped)
			{
				MarkTileDirty(TeamIndex, (GY / TileCells) * TilesX + (GX / TileCells));
			}
		}
	}
	VisibilityVersion.Increment();
}

// ── 查询 ──

bool UAsceFogSubsystem::IsCellVisible(int32 TeamKey, const FIntVector2& VisionCell) const
{
	const TArray<uint32>* Projection = GetTeamVisibleBits(TeamKey);
	if (!Projection || !HeightMap.IsValid())
	{
		return false;
	}
	if (VisionCell.X < 0 || VisionCell.X >= HeightMap.GetGridW() || VisionCell.Y < 0 || VisionCell.Y >= HeightMap.GetGridH())
	{
		return false;
	}
	const int32 GlobalIndex = VisionCell.X + VisionCell.Y * HeightMap.GetGridW();
	return ((*Projection)[GlobalIndex >> 5] & (1u << (GlobalIndex & 31))) != 0;
}

bool UAsceFogSubsystem::IsCellVisibleAtWorld(int32 TeamKey, const FVector2D& WorldPos) const
{
	if (!HeightMap.IsValid())
	{
		return false;
	}
	return IsCellVisible(TeamKey, HeightMap.WorldToCell(WorldPos));
}

bool UAsceFogSubsystem::RowSegmentAnyVisible(const TArray<uint32>& VisibleBits, int32 GridW, int32 RowY, int32 ColX0, int32 ColX1)
{
	if (RowY < 0 || ColX0 > ColX1)
	{
		return false;
	}

	const int32 StartX = FMath::Max(0, ColX0);
	if (StartX >= GridW)
	{
		return false;
	}
	const int32 EndX = FMath::Min(GridW - 1, ColX1);
	if (StartX > EndX)
	{
		return false;
	}

	const int32 RowBitBase = RowY * GridW;
	if (RowBitBase >= VisibleBits.Num() * 32)
	{
		return false; // 行超出位图覆盖（GridH 外）
	}

	const int32 W0 = (RowBitBase + StartX) >> 5;
	const int32 W1 = (RowBitBase + EndX) >> 5;

	for (int32 W = W0; W <= W1; ++W)
	{
		uint32 Word = VisibleBits[W];

		if (W == W0)
		{
			const int32 LowSkip = (RowBitBase + StartX) & 31;
			if (LowSkip != 0)
			{
				Word &= ~0u << LowSkip;
			}
		}
		if (W == W1)
		{
			const int32 HighKeep = (RowBitBase + EndX) & 31;
			if (HighKeep != 31)
			{
				Word &= (1u << (HighKeep + 1)) - 1;
			}
		}

		if (Word != 0)
		{
			return true;
		}
	}
	return false;
}

bool UAsceFogSubsystem::IsEntityVisible(int32 TeamKey, const FAsceVisionEntityGeometry& Geometry) const
{
	const TArray<uint32>* Projection = GetTeamVisibleBits(TeamKey);
	if (!Projection || !HeightMap.IsValid())
	{
		return false;
	}

	const int32 GridW = HeightMap.GetGridW();
	const int32 GridH = HeightMap.GetGridH();

	if (Geometry.bUseFootprint)
	{
		const int32 Y0 = FMath::Max(0, Geometry.FootprintY0);
		const int32 Y1 = FMath::Min(GridH - 1, Geometry.FootprintY1);
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			if (RowSegmentAnyVisible(*Projection, GridW, Y, Geometry.FootprintX0, Geometry.FootprintX1))
			{
				return true;
			}
		}
		return false;
	}

	// 单位：圆心 = entity 所在格，套圆行区间表
	const FIntVector2 CenterCell = HeightMap.WorldToCell(Geometry.CenterWorld);
	const int32 RGrid = AsceVision::CoverageRadiusToGrid(Geometry.CoverageRadiusCm, HeightMap.GetCellSizeCm());
	const TArray<AsceVision::FRowSpan>& Spans = AsceVision::GetCircleRowSpans(RGrid);

	const int32 SpanCount = Spans.Num(); // = 2*RGrid+1
	for (int32 i = 0; i < SpanCount; ++i)
	{
		const int32 YOff = i - RGrid;
		const int32 RowY = CenterCell.Y + YOff;
		if (RowY < 0 || RowY >= GridH)
		{
			continue;
		}
		const AsceVision::FRowSpan& Span = Spans[i];
		const int32 ColX0 = CenterCell.X + Span.XStart;
		const int32 ColX1 = CenterCell.X + Span.XEnd;
		if (RowSegmentAnyVisible(*Projection, GridW, RowY, ColX0, ColX1))
		{
			return true;
		}
	}
	return false;
}

#if !UE_BUILD_SHIPPING
int32 UAsceFogSubsystem::GetNumActiveResults() const
{
	return ActiveResults.Num();
}
#endif
