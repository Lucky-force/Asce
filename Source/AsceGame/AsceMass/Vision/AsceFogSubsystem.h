// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassExternalSubsystemTraits.h"
#include "AsceVisionDefines.h"
#include "AsceVisionHeightMap.h"
#include "AsceFogSubsystem.generated.h"

class UAsceFogProcessor;

// ══════════════════════════════════════════════════════════════════════════════
// MOD-POINT 2026-09-09 17:20:39
// 目标数据结构（格中心；本步只新增类型定义，不接入既有逻辑，可安全编译）。
// 结构对应用户确认原文：每格 = 并发单元；每队 slot 同时承载"目标可见计数"与"源结果"。
// ══════════════════════════════════════════════════════════════════════════════

/** 源格上某档 (He,R) 的结果：flood 位图 + 占用单位数（原全局 ParmsCounts 的归属）。 */
struct FGridResult
{
	TArray<uint32> Bits;    // 该 (He,R) 档的 flood 输出位图（窗口相对本格）
	int32 Occupants = 0;    // 占用此档的单位数（决定下线时机）
};

/** 每格每队的数据：一个队伍 slot 同时管"本格作为目标格被覆盖"与"本格作为源格的结果"。 */
struct FTeamCellData
{
	// 本格作为目标格：被该队多少个源贡献覆盖（>0 即可见）——原 Counts[team][格] 的权威版
	uint16 VisibilityCount = 0;

	// 本格作为源格：该队在此格的不同 (He,R) 档结果（仅当有单位站这格才非空）
	TMap<uint64 /*(He<<16)|R*/, FGridResult> SourceParms;
};

/** 每格数据：锁 + 按队伍 index 的惰性 Teams（空格空 TArray，出现 index 队伍才 SetNum(index+1)）。 */
struct FGridInfo
{
	FCriticalSection Lock;            // 保护本格一切写

	TArray<FTeamCellData> Teams;      // 按队伍 index 直接下标

	FTeamCellData& EnsureTeamSlot(int32 TeamIndex)
	{
		if (TeamIndex >= Teams.Num())
		{
			Teams.SetNum(TeamIndex + 1, EAllowShrinking::No); // 惰性扩张，空洞很小
		}
		return Teams[TeamIndex];
	}

	FTeamCellData* FindTeamSlot(int32 TeamIndex)
	{
		return Teams.IsValidIndex(TeamIndex) ? &Teams[TeamIndex] : nullptr;
	}
};

/** 活跃源档位置（格内 SourceParms 条目的枚举句柄，供遮挡翻转遍历；按 (TeamKey,格,He,R) 定位）。 */
struct FActiveKey
{
	int32 TeamKey = 0;
	int32 CellIndex = -1;
	uint16 He = 0;
	uint16 R = 0;

	bool operator==(const FActiveKey& Other) const
	{
		return TeamKey == Other.TeamKey && CellIndex == Other.CellIndex && He == Other.He && R == Other.R;
	}

	/** 全序（用于 ActiveResults 有序数组的二分查找）：CellIndex → TeamKey → He → R。 */
	bool operator<(const FActiveKey& Other) const
	{
		if (CellIndex != Other.CellIndex) return CellIndex < Other.CellIndex;
		if (TeamKey != Other.TeamKey) return TeamKey < Other.TeamKey;
		if (He != Other.He) return He < Other.He;
		return R < Other.R;
	}
};

/** 活跃/可见性的帧末变更操作（并行阶段 push，单线程统一处理：按档的最终 Occupants 决定 +1/−1 与 ActiveResults 增删）。 */
struct FChangeOp
{
	FActiveKey Key;
	bool bRemove = false;
	TArray<uint32> RemoveBits; // 仅 remove（删档）时需要：旧位图用于撤销
};

/** FChangeOp 分片缓冲。 */
struct FChangeOpShard
{
	volatile int32 Lock = 0;
	TArray<FChangeOp> Items;
};

/** entity 可见性判定用的几何：单位 = 位置 + 物理覆盖半径（圆）；建筑 = footprint（AABB，视野格，含边界）。 */
USTRUCT(BlueprintType)
struct FAsceVisionEntityGeometry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	FVector2D CenterWorld = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadWrite)
	float CoverageRadiusCm = 0.f;

	UPROPERTY(BlueprintReadWrite)
	bool bUseFootprint = false;

	UPROPERTY(BlueprintReadWrite)
	int32 FootprintX0 = 0;
	UPROPERTY(BlueprintReadWrite)
	int32 FootprintY0 = 0;
	UPROPERTY(BlueprintReadWrite)
	int32 FootprintX1 = 0;
	UPROPERTY(BlueprintReadWrite)
	int32 FootprintY1 = 0;
};

/** 动态遮挡重建的单块写：一块矩形区域的遮挡高度（视野格坐标，含端）。 */
struct FAsceOcclusionRangeWrite
{
	int32 X0 = 0;
	int32 Y0 = 0;
	int32 X1 = 0;
	int32 Y1 = 0;
	uint16 OcclusionHeightCm = 0;
};

/**
 * 视野/迷雾 Subsystem（架构文档落地；UTickableWorldSubsystem，Server 与 Client 各一份、跑同一套确定性计算）。
 *
 * 职责：
 *  - 持有高度场（FAsceVisionHeightMap，本地初始化）；
 *  - flood 核心：ComputeVisionForCell —— 给定源格/视点高度/视野半径，算可见掩码；
 *  - per-team 可见集：覆盖计数图 + 派生可见位图；
 *  - 结果池 / 贡献维护——由 AsceFogProcessor 每帧驱动；
 *  - 动态遮挡重建 + 翻转失效；
 *  - IsCellVisible / IsEntityVisible。
 *
 * 数据结构说明：所有容器都保持"单层"，key 内含 (TeamKey, 格, He, R)，避免 TMap 嵌套做 value
 * （嵌套 TMap 会让 UHT 生成的默认构造在部分工具链上报 C2512，见提交记录）。
 *
 * v1 简化：
 *  - 结果池不设"待回收 TTL"，引用归零立即删除；结果引用数 = 引用它的 (team,格) 数；
 *  - 同 (格,He,R) 的几何结果不跨队伍共享（每队各自存一份，实现简单；队伍数少，实测吃紧再合并）。
 *
 * 驱动方式（与 UAsceSpatialGridSubsystem 相同的模式）：本类是 UTickableWorldSubsystem，
 * OnWorldBeginPlay 创建 UAsceFogProcessor 并 CallInitialize；Tick 里手动
 * UE::Mass::Executor::Run 驱动它（不入 Mass 全局调度）。
 */
UCLASS()
class UAsceFogSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UAsceFogSubsystem();

	// UTickableWorldSubsystem Interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~UTickableWorldSubsystem Interface

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;

	static UAsceFogSubsystem* Get(const UObject* WorldContextObject);

	// ── 地图尺寸 / 高度场 ──

	/** 建立视野地图（此后尺寸固定）。调用时机 = 本地地图数据就绪（关卡加载期，双端各自调）。 */
	void InitializeVisionMap(int32 GridW, int32 GridH, float VisionCellSizeCm = AsceVision::DefaultVisionCellSizeCm);

	bool IsReady() const { return HeightMap.IsValid(); }

	FAsceVisionHeightMap& GetHeightMap() { return HeightMap; }
	const FAsceVisionHeightMap& GetHeightMap() const { return HeightMap; }

	/** 视点高度 H_e = 源格有效高度 + 眼睛离地高度，钳到 uint16。 */
	static uint16 ComputeHe(uint16 SourceCellEffectiveHeight, uint16 EyeHeightCm);

	// ── flood 核心 ──

	static int32 GetWindowSize(int32 R) { return 2 * R + 1; }

	/**
	 * 计算源格 S 的可见掩码：圆内 BFS，墙格(Hv>He)自身标记可见但不透传。
	 * OutBits 大小 = ceil(Window² / 32)。调用方负责清零/复用。
	 */
	static void ComputeVisionForCell(const FAsceVisionHeightMap& HMap, const FIntVector2& S, uint16 He, uint16 R, TArray<uint32>& OutBits);

	// ── per-team 可见位图投影 ──

	/** 只读访问某 team 的可见位图投影（行主序、1bit/格）。不存在则返回空。 */
	const TArray<uint32>* GetTeamVisibleBits(int32 TeamKey) const;

	/** 可见集版本号：任一次可见位图投影变更时递增（渲染端据此判断是否需要重传）。 */
	int32 GetVisibilityVersion() const { return VisibilityVersion.GetValue(); }

	// ── 脏 tile（2D 方块，供渲染端增量上传；见架构文档） ──
	// 说明：投影是平坦打包（bit = x + y*GridW），方块不是连续内存且字会跨行，
	// 所以 CPU 在发送脏 tile 时把 64×64 bit 重打包成规范布局（每行 2 word，共 128 word）。
	// 只对脏 tile 做 repack，成本可忽略；换来脏粒度与横/纵变化无关。

	/** tile 边长（格），必须是 32 的倍数。 */
	static constexpr int32 TileCells = 64;
	static constexpr int32 TileWordsPerRow = TileCells / 32;   // 2
	static constexpr int32 TileWords = TileCells * TileWordsPerRow; // 128

	int32 GetTilesX() const { return TilesX; }
	int32 GetTilesY() const { return TilesY; }
	int32 GetNumTiles() const { return TilesX * TilesY; }

	/** 取走并清空某队的脏 tile 列表（GT 单线程；与 ApplyBitsToTeamCounts 标脏同线程，无并发窗口）。 */
	void DrainDirtyTiles(int32 TeamKey, TArray<int32>& OutTileIndices);

	/** 拷出某 tile 的 repacked word（每行 TileWordsPerRow 个 word，行主序；边缘补 0）。越界/非法队输出全 0。 */
	void ExtractTileWords(int32 TeamKey, int32 TileIndex, TArray<uint32>& OutWords) const;

	/** 把某队所有 tile 标脏（切队/首次全量重建用）。 */
	void MarkAllTilesDirty(int32 TeamKey);

	// ── 单位视野贡献（§5.2/§5.3，由 AsceFogProcessor / Remove Observer 传参驱动） ──
	// 说明：Fog 侧**不维护**"单位 → 贡献记录"的集中表——单位上帧贡献参数存在它自己的
	// FAsceVisionFogStateFragment 上（见 AsceVisionFragments.h）。这里只提供给定
	// (队,格,He,R) 的上/下线原语，由调用方负责"用旧参数下线、新参数上线"。

	/** 上线一个贡献：该 (team,cell,He,R) 首次出现时在源格锁内算好结果并登记 ActiveResults。CellIndex<0（界外）忽略。 */
	void AddContribution(int32 TeamKey, int32 CellIndex, uint16 He, uint16 R);

	/** 下线一个贡献：该 (team,cell,He,R) 最后一个引用离开时 apply −1 并回收结果。CellIndex<0 忽略。 */
	void RemoveContribution(int32 TeamKey, int32 CellIndex, uint16 He, uint16 R);

	// ── 动态遮挡增删与翻转（由遮挡实体 Observer 事件驱动，无整层重建） ──

	/**
	 * 一个遮挡实体（建筑）入场：把其 footprint 区域写入动态遮挡层（局部）。
	 * 区域会进内部 OcclusionDirty，供帧末 ProcessHeightChanges 翻转失效。
	 */
	void AddOcclusionRect(const FAsceOcclusionRangeWrite& Write);

	/**
	 * 一个遮挡实体（建筑）退场：清空其 footprint 区域（建筑 footprint 互不重叠，直接清零无残留）。
	 * 调用方（Observer Remove 回调）在实体数据仍在时读 footprint 区域传入。
	 */
	void RemoveOcclusionRect(const FAsceOcclusionRangeWrite& Rect);

	/** 帧末：对内部 Dirty 格命中的活跃结果做原位重算，并把新旧位图差异应用到引用它的各 (team,格)。 */
	void ProcessHeightChanges();

	// ── 查询（§4.1） ──

	bool IsCellVisible(int32 TeamKey, const FIntVector2& VisionCell) const;
	bool IsCellVisibleAtWorld(int32 TeamKey, const FVector2D& WorldPos) const;
	bool IsEntityVisible(int32 TeamKey, const FAsceVisionEntityGeometry& Geometry) const;

#if !UE_BUILD_SHIPPING
	// 调试统计
	int32 GetNumActiveResults() const;
#endif

private:
	// ── 内部状态 ──
	// 源结果已迁入格内 FGridInfo.Teams[].SourceParms；活跃条目用 FActiveKey（见类外定义）枚举。

	// —— helper ——

	/** 从打包 TeamKey 拆出队伍 index（高 16 位）；非法 key 可能返回负/越界值，各使用点必须自守卫。 */
	FORCEINLINE int32 GetIndex(int32 TeamKey) const { return AsceVision::GetTeamIndex(TeamKey); }

	/** 把一份结果位图应用/撤销到目标格（每格加锁：GridInfos[格].Teams[].VisibilityCount）+ 投影位（Sign=+1/-1）。 */
	void ApplyBitsToTeamCounts(int32 TeamKey, const FIntVector2& Center, uint16 R, const TArray<uint32>& Bits, int32 Sign);

	/** 标脏某队某 tile（GT 单线程；先读判断是否已脏，已脏则跳过写）。 */
	void MarkTileDirty(int32 TeamIndex, int32 TileIndex);

	/** 并行阶段把一条档变更塞进空闲分片（试锁；全忙则阻塞第一片）。 */
	void PushPendingChange(const FActiveKey& Key, bool bRemove, TArray<uint32>&& RemoveBits);

	/** 帧末单线程：drain 变更分片，按档的最终存在性更新 ActiveResults 与可见 ±apply。 */
	void ProcessPendingChanges();

	/** 把 [X0,Y0,X1,Y1]（含端）区域内所有格索引加入 OcclusionDirty（自动 clamp 到图内）。 */
	void MarkOcclusionDirtyRect(int32 X0, int32 Y0, int32 X1, int32 Y1);

	/** 行段批量非零测试：可见位图第 RowY 行，[ColX0, ColX1] 闭区间内是否有置位（§4.1）。 */
	static bool RowSegmentAnyVisible(const TArray<uint32>& VisibleBits, int32 GridW, int32 RowY, int32 ColX0, int32 ColX1);

	// —— 成员 ——

	FAsceVisionHeightMap HeightMap;

	// —— 目标结构（格中心）落地容器 ——
	/** 格中心权威数据：每格锁 + 惰性 Teams[index]（含 VisibilityCount 与未来的 SourceParms）。 */
	TArray<FGridInfo> GridInfos;

	/** per-team 全图可见位图投影：InitializeVisionMap 时固定分配 [MaxTeamCount]（行主序 1bit/格）。 */
	TArray<TArray<uint32>> TeamProjections;

	/** tile 网格尺寸 = ceil(Grid / TileCells)。 */
	int32 TilesX = 0;
	int32 TilesY = 0;

	/** per-team 脏 tile 位图（每字 32 tile）。 */
	TArray<TArray<uint32>> TeamDirtyTileWords;

	/** 任一次可见位图投影变更时递增（渲染端轮询判断重传）。 */
	FThreadSafeCounter VisibilityVersion;

	/** 活跃源档枚举（遮挡翻转遍历用）；本体的增删只在帧末单线程处理变更时发生。 */
	TArray<FActiveKey> ActiveResults;

	/** 并行阶段待处理的档变更分片（每片自旋锁；帧末单线程处理：更新 ActiveResults 与可见 ±apply）。 */
	TArray<FChangeOpShard> PendingChangeShards;

	/**
	 * 本帧遮挡变化格（Add/RemoveOcclusionRect 事件写入），**按 GridIndex 升序、去重**。
	 * 用有序数组而不是 TSet：ProcessHeightChanges 对每个结果只需二分定位
	 * [结果外接 index 下界, 上界] 的起点后线性扫那一小段，不必和全量 dirty 比较。
	 * ProcessHeightChanges 消费后清空。
	 */
	TArray<int32> OcclusionDirty;

	/** 手动驱动的 FogProcessor（与 UAsceSpatialGridSubsystem 同模式）。 */
	UPROPERTY(Transient)
	TObjectPtr<UAsceFogProcessor> FogProcessor;
};

template<>
struct TMassExternalSubsystemTraits<UAsceFogSubsystem>
{
	enum
	{
		GameThreadOnly = false, // 单位 diff 走 ParallelForEachEntityChunk；写路径已在格锁/分片保护
	};
};
