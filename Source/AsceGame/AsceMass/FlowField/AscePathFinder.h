// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MassEntityHandle.h"
#include "AsceFlowFieldCores.h"
#include "AscePathFinder.generated.h"

/**
 * 
 */
struct FSourcePosition
{
	FSourcePosition() = default;
	FSourcePosition(FSector::FSectorID InSectorID, FIslandField InIslandField)
	: SectorID(InSectorID), IslandField(InIslandField)
	{
			
	}

	bool operator==(const FSourcePosition& Other) const
	{
		return SectorID == Other.SectorID && IslandField == Other.IslandField;
	}

	friend uint32 GetTypeHash(const FSourcePosition& Pos)
	{
		return HashCombine(GetTypeHash(Pos.SectorID.RawID), GetTypeHash(Pos.IslandField.IslandId));
	}
		
	FSector::FSectorID SectorID;
	FIslandField IslandField;
};


struct FPortalPathResult
{
	FSourcePosition SourcePosition;
	bool bSuccess = false;
	//从Source侧代表Portal走到Goal侧代表Portal的正向Portal序列；如果Source和Goal本来就在同一Sector，这里会是空数组
	TArray<FPortal::FPortalIndex> PortalChain;
};

struct FPortalSearchNodeData
{
	float BestG = 0.f;
	FPortal::FPortalIndex CameFrom;
	uint32 SearchStamp = 0; // 0 = 从未使用过；等于CurrentSearchStamp才算"本次搜索有效"
};

/** 一批合并A*请求(单目标+多源)算完之后留下的、可以被运行时持续复用的"寻路方案"：
 * 记录了Goal自身的位置信息，以及这批请求里所有被访问过的Portal各自的"下一跳Portal"。
 * entity每帧只需要拿着对应的FMergedPathPlanHandle + 自己当前的世界坐标，就能查到自己现在应该朝哪个方向走
 * (见UPathFinder::TryGetFlowDirection)，完全不需要维护任何"走到第几段了"的进度状态——
 * 因为查询键是(当前SectorID+当前IslandField)而不是链上的顺序位置，天然没有歧义，也天然可重入
 * (entity被击退/传送到任意已覆盖位置都能立刻查到正确方向)。
 * Plan本身由UPathFinder内部持有和管理生命周期，外部只拿一个Handle，见下方FMergedPathPlanHandle */
/** 某扇区"到 goal 的 LOS 覆盖层"（每格 1 bit）。用于：entity 在该格能直视 goal 时直接朝 goal 转向，
 *  忽略 (扇区,portal) 流场。Blocked 为阴影线标记（计算期用；运行时只用 HasLOS）。 */
struct FSectorGoalLOS
{
	TBitArray<> HasLOS;         // SECTOR_SIZE*SECTOR_SIZE，true=该格可直视 goal
	TBitArray<> Blocked;        // true=被阴影线挡住（波前到此为止）
	TBitArray<> FrontierWalls;  // true=视线边缘的墙（变了就可能影响视线，用于失效过滤）
	FSector::FSectorID SectorIDAtComputeTime; // 计算时的扇区ID(含时间戳)，用于失效判断
};

struct FMergedPathPlan
{
	FSourcePosition GoalSourcePosition;
	FVector2D GoalWorldLocation = FVector2D::ZeroVector;
	//记录每个已知Portal通向Goal的下一跳Portal；某个(Sector,Island)对应的Portal如果不在这里，
	//说明它没有被这批请求覆盖到，需要为这个位置重新发起一次RunMergeablePathRequest
	TMap<FPortal::FPortalIndex, FPortal::FPortalIndex> NextPortalTowardsGoal;
	
	TMap<FSourcePosition, FPortal::FPortalIndex> SectorIslandToExitPortal;

	// 到 goal 的 LOS 覆盖层（仅 goal 邻近 K 环内、且路线经过的扇区）。entity 落在 HasLOS 格上时直接朝 goal。
	TMap<FSector::FSectorID, FSectorGoalLOS> GoalLOS;
};

/** FMergedPathPlan的外部句柄，代表一次"目标+多源"的寻路方案。
 * 所有使用同一条路径的entity共享同一个Handle来查询方向(TryGetFlowDirection)，不需要各自持有Plan数据。
 * 引用计数语义：谁开始使用这个Handle(比如entity决定沿它走)就调用一次AcquirePathPlan，
 * 不再使用时(entity到达/切换目标/死亡)调用一次ReleasePathPlan。计数归零时Plan才会被回收；
 * Plan回收时会释放它对相关FlowFieldResult持有的强引用，如果那之后真的没有别的地方
 * (其它Plan/其它引用)还在用这些FlowFieldResult了，FlowFieldCache里对应的弱引用条目
 * 会在下一次CleanupFlowFieldCache()例行清理时被自然移除——不需要为FlowField单独写一套清理逻辑 */
struct FMergedPathPlanHandle
{
	uint32 RawID = 0;

	bool IsValid() const { return RawID != 0; }
	bool operator==(const FMergedPathPlanHandle& Other) const { return RawID == Other.RawID; }
};

/** UPathFinder内部实际持有的Plan条目：Plan数据 + 引用计数 + 这个Plan曾经查询过的FlowFieldResult强引用表。
 * RetainedFlowFields用FlowFieldKey去重(同一个Key多次查询只覆盖，不重复持有)，
 * 并且每次查询都会用GetOrComputeFlowField的最新返回值覆盖旧的强引用——
 * 这样"是否过期"的判断逻辑完全交给GetOrComputeFlowField/FlowFieldCache已有的TimeStamp校验，
 * 这里只负责"帮忙攥着，别让它在两帧之间被弱引用机制提前回收掉"
 * (定义放在FFlowFieldKey/FFlowFieldResult下面，因为要用到它们) */

// 流场的目标：要么是要走向的某个Portal（对应一整段边界格子），要么是Goal所在的具体格子坐标；两者互斥，不再各占一个字段
using FFlowFieldTarget = TVariant<FPortal::FPortalIndex, FIntVector2>;

/** 流场缓存的查找键：不含TimeStamp，避免版本变化后旧Key永远留在Map里 */
struct FFlowFieldKey
{
	uint16 SectorRawID = 0;
	FFlowFieldTarget Target;

	bool operator==(const FFlowFieldKey& Other) const
	{
		if (SectorRawID != Other.SectorRawID || Target.GetIndex() != Other.Target.GetIndex())
		{
			return false;
		}
		if (Target.IsType<FPortal::FPortalIndex>())
		{
			return Target.Get<FPortal::FPortalIndex>() == Other.Target.Get<FPortal::FPortalIndex>();
		}
		return Target.Get<FIntVector2>() == Other.Target.Get<FIntVector2>();
	}

	friend uint32 GetTypeHash(const FFlowFieldKey& Key)
	{
		uint32 Hash = HashCombine(GetTypeHash(Key.SectorRawID), GetTypeHash(Key.Target.GetIndex()));
		if (Key.Target.IsType<FPortal::FPortalIndex>())
		{
			Hash = HashCombine(Hash, GetTypeHash(Key.Target.Get<FPortal::FPortalIndex>()));
		}
		else
		{
			Hash = HashCombine(Hash, GetTypeHash(Key.Target.Get<FIntVector2>()));
		}
		return Hash;
	}
};

/** 流场结果 */
struct FFlowFieldResult
{
	FSector::FSectorID SectorID;
	FFlowFieldTarget Target;
	TArray<FFlowField, TInlineAllocator<SECTOR_SIZE * SECTOR_SIZE>> Directions; // SECTOR_SIZE*SECTOR_SIZE，每格一个量化方向+可通行/LOS标记
	
#if WITH_EDITORONLY_DATA
	TArray<uint16, TInlineAllocator<SECTOR_SIZE * SECTOR_SIZE>> IntegrationCosts; // 每格的积分代价快照，MAX_uint16=未到达
#endif
};

struct FFlowFieldCacheEntry
{
	FSector::FSectorID SectorIDAtComputeTime; // 计算那一刻的完整快照(带TimeStamp)，用CheckTimeStamp判断是否过期
	TWeakPtr<FFlowFieldResult> WeakResult;
};

/** UPathFinder内部实际持有的Plan条目：Plan数据 + 引用计数 + 这个Plan曾经查询过的FlowFieldResult强引用表 */
struct FMergedPathPlanEntry
{
	FMergedPathPlan Plan;

	/** 本方案的组成员（下单时登记；只增不减——已到达单位虽然释放了Handle，但仍是组员，
	 *  是 disruption（传送/死亡等）重置的主要对象）。条目有效性由使用方逐项校验：
	 *  entity有效（EntityHandle 含序列号，防 index 复用）+ 其 PathPlanHandle.RawID 与本 plan 一致。 */
	TArray<FMassEntityHandle> Members;

	/** 到达判定半径的平方：只在"首次标记到达"时更新为 max(旧值, 自己到目标距离 + 自己直径 + 余量)。
	 *  未到达单位进入该半径后才做"与已到达单位的接触检测"；已到达单位每帧把自己的距离与它比较，
	 *  超过即判定被顶飞/离位，回退寻路。0 = 还没有人到达（路径2不可用）。 */
	float ArrivalRadiusSq = 0.f;

	TMap<FFlowFieldKey, TSharedPtr<FFlowFieldResult>> RetainedFlowFields;
	int32 RefCount = 0;
};

UCLASS()
class UPathFinder : public UObject
{
	GENERATED_BODY()
	
public:
	
	void SetWorldMapData(FWorldMapData* InWorldMapData) { WorldMapData = InWorldMapData; }
	
	// 当前绑定的世界数据（可能被RegisterFlowFieldDebugger换网到Debugger的数据上）
	FWorldMapData* GetWorldMapData() const { return WorldMapData; }
	
	/** 发起一批合并A*请求(单目标+多源)，内部创建一个FMergedPathPlan并返回它的Handle供后续每帧查询方向用。
	 * OutResults可传nullptr：运行时的高频路径（entity每帧查方向）完全不需要它，
	 * 只有下面这些场景才用得上——1) 调用方想立刻知道某个Source此刻是否可达(bSuccess)；
	 * 2) 调试可视化想画出某个Source当时具体走的PortalChain。
	 * 返回的Handle初始引用计数为0，调用方需要为每个打算使用它的entity调用一次AcquirePathPlan，
	 * 否则下一次CleanupFlowFieldCache的例行清理会把它当成"从未被认领"的Plan直接回收掉 */
	FMergedPathPlanHandle RunMergeablePathRequest(const FVector2D& GoalWorldLocation, const TArray<FVector2D>& EntityWorldLocations, TArray<FPortalPathResult>* OutResults = nullptr);

	/** entity决定开始沿着某个Plan走时调用一次，让Plan的引用计数+1，防止它被提前回收 */
	void AcquirePathPlan(FMergedPathPlanHandle Handle);

	/** entity不再需要某个Plan时调用一次(到达/切换目标/死亡)，引用计数-1，归零则立即回收该Plan
	 * (连带释放它对相关FlowFieldResult的强引用；FlowFieldResult本身是否真正被清理，
	 * 交给FlowFieldCache已有的弱引用机制在下次CleanupFlowFieldCache时判断) */
	void ReleasePathPlan(FMergedPathPlanHandle Handle);

	/** 登记方案的组成员（下单时由 FAsceMoveOrderExecutor 调用；只增不减，见 FMergedPathPlanEntry::Members） */
	void RegisterPlanMembers(FMergedPathPlanHandle Handle, const TArray<FMassEntityHandle>& InMembers);

	/** 取方案的组成员名单；plan不存在返回nullptr（条目有效性由调用方校验） */
	const TArray<FMassEntityHandle>* GetPlanMembers(FMergedPathPlanHandle Handle) const;

	/** 方案是否还存在（只查不创建，不改变引用计数）。disruption 重置/顶飞回退的"查不到就放弃"用它 */
	bool IsPathPlanAlive(FMergedPathPlanHandle Handle) const;

	/** 取到达判定半径平方；plan不存在返回0 */
	float GetPlanArrivalRadiusSq(FMergedPathPlanHandle Handle) const;

	/** 到达判定半径平方只增更新（首次标记到达时调用） */
	void GrowPlanArrivalRadiusSq(FMergedPathPlanHandle Handle, float InRadiusSq);

	/** 清空到达判定半径平方（disruption 组重置时调用，重新从"第一个到点的人"开始长） */
	void ResetPlanArrivalRadiusSq(FMergedPathPlanHandle Handle);

	/** 运行时消费端接口：每帧每个entity只需要传入自己当前的世界坐标 + 持有的FMergedPathPlanHandle，
	 * 就能查到它现在应该朝哪个方向走，不需要维护任何"当前走到第几段"的状态。
	 * 返回false的情况：Handle无效/对应Plan已被回收；EntityWorldLocation不在地图范围内/落在墙上/
	 * 落在死区格子里；或者这个位置所在的(Sector,Island)完全没有被Plan覆盖到
	 * (entity跑出了这批请求规划的范围，需要重新发起路径请求) */
	bool TryGetFlowDirection(FMergedPathPlanHandle Handle, const FVector2D& EntityWorldLocation, FVector2D& OutDirection);

	/** 每帧调用：最多重算 Budget 个"视线失效"的 plan（队列去重、跳过已回收 plan）。 */
	void ProcessGoalLOSRefreshQueue(int32 Budget);
	
protected:

	/**
	 * Mergeable A*
	 */
	
	/** 获取Portal的等效Location */
	FVector2D GetPortalRepresentativeWorldPosition(const FPortal& Portal) const;

	ASCE_INLINE FSourcePosition GetSourcePosition(const FVector2D& WorldLocation) const
	{
		FIntVector2 GridLocation = WorldMapData->WorldLocationToGridLocation(WorldLocation);
		FSector::FSectorID SectorID = WorldMapData->GridLocationToSectorId(GridLocation);
		if (!SectorID.IsValid())
		{
			return FSourcePosition(FSector::InvalidSectorID, FSector::InvalidIslandField);
		}
		uint32 LocalIndex = WorldMapData->GridLocationToGridIndexInSector(GridLocation);
		const FSector& Sector = WorldMapData->GetSectorData(SectorID);
		return FSourcePosition(SectorID, Sector.IslandFields[LocalIndex]);
	}

	void GetSourcePositionSet(const TArray<FVector2D>& EntityWorldLocations, TSet<FSourcePosition>& OutSet) const;
	
	/** 获取Source位置对应的Portal：
	 * 默认返回该(扇区,岛屿)的第一个匹配Portal（索引序）；若SourceWorldLocation非空，
	 * 多个匹配Portal时选代表点离source世界位置最近的那个（源侧语义：选离单位近的出口）。 */
	FPortal::FPortalIndex GetPortalId(const FSourcePosition& SourcePosition, const FVector2D& SourceWorldLocation) const;

	/** 获取Portal的可到达Neighbors以及它们的权重 */
	void GetPortalGraphNeighbors(FPortal::FPortalIndex PortalIndex, TArray<TPair<FPortal::FPortalIndex, float>>& OutNeighbors) const;

	ASCE_INLINE static float GetManhattanDistance(const FVector2D& A, const FVector2D& B)
	{
		return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y);
	}

	/** 从PortalSearchScratch的CameFrom字段里反向回溯出 [StartIndex ... EndIndex] 的正向序列，追加到OutChain末尾 */
	void ReconstructChain(FPortal::FPortalIndex EndIndex, FPortal::FPortalIndex StartIndex, const TArray<FPortalSearchNodeData>& Scratch, TArray<FPortal::FPortalIndex>& OutChain) const;

	/** 沿着NextPortalTowardsGoal已知的后续链条一路走到Goal，追加到OutChain末尾（用于"合并"命中时拼接） */
	void AppendKnownSuffix(FPortal::FPortalIndex StartOfSuffix, const TMap<FPortal::FPortalIndex, FPortal::FPortalIndex>& NextPortalTowardsGoal, const FSector::FSectorID& SkipSector, TArray<FPortal::FPortalIndex>& OutChain) const;

	/** 合并A*的单体处理 */
	bool RunSingleMergeableAStar(FPortal::FPortalIndex StartPortalIndex, const FSourcePosition& GoalSourcePosition, const FVector2D& GoalWorldLocation, TMap<FPortal::FPortalIndex, FPortal::FPortalIndex>& NextPortalTowardsGoal, TArray<FPortal::FPortalIndex>& OutChain) const;

	/** 让PortalSearchScratch数组大小跟当前世界的真实Portal总数(WorldMapData->Portals.Num())对齐；只在真正需要扩容时才SetNum */
	void EnsurePortalSearchScratchSized() const;

	// 以下两个成员标记为mutable：RunMergeablePathRequest/RunSingleMergeableAStar都是const成员函数，
	// 但它们需要写入scratch数组、递增搜索戳。这是"内部可复用缓存，不影响对外可观察结果"的典型mutable用途。
	mutable TArray<FPortalSearchNodeData> PortalSearchScratch;
	mutable uint32 CurrentSearchStamp = 0;


	/**
	 * Eikonal Flow Field
	 */
public:

	/** 从一条PortalChain（含Source/Goal所在Sector）里，把需要计算流场的每一段提取出来并返回结果（命中缓存则直接复用）
	 * GoalGridLocation：Goal的真实格子坐标，只在Goal所在Sector那一段（Target是格子坐标而非Portal）会用到 */
	void RequestFlowFieldsForChain(uint16 SourceSectorRawID, uint16 GoalSectorRawID, const FIntVector2& GoalGridLocation, const TArray<FPortal::FPortalIndex>& PortalChain, TArray<TSharedPtr<FFlowFieldResult>>& OutResults);

protected:

	/** 查缓存 -> CheckTimeStamp校验 -> Pin()复用；未命中或已过期则原地重算并覆盖该Key */
	TSharedPtr<FFlowFieldResult> GetOrComputeFlowField(uint16 SectorRawID, const FFlowFieldTarget& Target);

	/** 真正的Eikonal积分（LOS Pass / Cost Integration Pass / Flow Field Pass），尚未实现
	 * Target是Portal：种子是Portal.PortalBitMap覆盖的整段边界格子（不收缩成单点）
	 * Target是格子坐标：种子就是这一个格子 */
	void ComputeFlowFieldInto(FSector::FSectorID SectorID, const FFlowFieldTarget& Target, FFlowFieldResult& OutResult) const;

	/** goal 邻域上限：只对"路线经过 且 扇区曼哈顿距离 goal ≤ 该值"的扇区铺 LOS（有界，一帧可完成）。 */
	static constexpr int32 GoalLOSMaxSectorRing = 3;

	/** 有界全局 LOS：从 GoalGridLocation 在 LosSectors 范围内做波前+角点阴影线（全局坐标，线自然跨扇区续），
	 *  把每格的 HasLOS/Blocked 按扇区拆进 OutGoalLOS。 */
	void ComputeGoalLOSOverlay(const FIntVector2& GoalGridLocation, const TSet<FSector::FSectorID>& LosSectors,
		TMap<FSector::FSectorID, FSectorGoalLOS>& OutGoalLOS) const;

	/** 由 plan 的路线扇区（∩ goal K 环）重算整份 goal LOS 覆盖层。A* 后与懒失效刷新共用。 */
	void BuildGoalLOSForPlan(FMergedPathPlan& Plan) const;

	/** 旧覆盖层判断：当前扇区的改动是否影响视线（原可见格变墙，或原视线边缘墙消失）。 */
	bool GoalLOSAffectedByChange(const FMergedPathPlan& Plan, FSector::FSectorID SectorID) const;

	/** goal LOS 重算队列（升序 plan RawID，二分插入去重）。 */
	void EnqueueGoalLOSRefresh(uint32 PlanRawID);
	bool IsGoalLOSRefreshQueued(uint32 PlanRawID) const;

	using FIntegrationFieldArray = TArray<FIntegrationField, TInlineAllocator<SECTOR_SIZE * SECTOR_SIZE>>;

	/** LOS Pass：从SeedLocalGridLocation（Sector本地坐标，[0,SECTOR_SIZE)）出发做BFS波前扩散，
	 * 标记LineOfSight/LOSWaveFrontBlocked，只在Sector内部跑，不跨Sector传播 */
	void RunLOSPass(const FSector& Sector, const FIntVector2& SeedLocalGridLocation, FIntegrationFieldArray& OutIntegrationFields) const;

	/** 从CornerVertex（障碍格朝向来源那条近边上的某一个顶点，顶点坐标系）出发，沿"种子格心->CornerVertex"方向延伸一条线，
	 * 把沿途格子标记为LOSWaveFrontBlocked，直到走出Sector边界 */
	static void MarkShadowLine(const FIntVector2& SeedLocalGridLocation, const FVector2D& CornerVertex, FIntegrationFieldArray& OutIntegrationFields);

	/** Cost Integration Pass：按照Jeong & Whitaker的Fast Iterative Method(FIM)论文Algorithm 3.2实现——
	 * 维护一个活跃列表(Active List)，每轮同步地对列表里每个点用它当前4个邻居的值重新求解一次候选代价，
	 * 代价不再变化(收敛)就移出列表并尝试把邻居拉进来，没收敛就留到下一轮继续算，直到活跃列表清空。
	 * Target是Goal格子：IntegrationFields须是RunLOSPass算完的结果，LOS格子相当于FIM里"已固定"的Source点，
	 * 只读不改，把它们的邻居拉进活跃列表作为起点；
	 * Target是Portal：IntegrationFields是空/未初始化的，这里会自己做全量初始化，
	 * 种子是PortalBitMap覆盖的整段边界格子（同样固定为Source，代价0，只有其邻居进活跃列表） */
	void RunCostIntegrationPass(const FSector& Sector, const FFlowFieldTarget& Target, FIntegrationFieldArray& IntegrationFields) const;

	/** Flow Field Pass：根据Cost Integration Pass算好的积分场，给每个格子算出一个量化方向（16方向，见FFlowField::EDirection）。
	 * LOS格子：直接用"格子中心->GoalLocalGridLocation"的连续向量量化，比8方向邻居比较更精确；
	 * 非LOS格子：比较8个邻居(4正交+4对角)的积分代价，朝最便宜的那个走；对角邻居要求两侧正交格子都不是墙，避免抄墙角近道；
	 * 墙/积分场没覆盖到的死区格子：标记为不可通行(Pathable位清空)。
	 * GoalLocalGridLocation只在Target是格子坐标时才有意义，Target是Portal时传任意值即可（不会被用到，因为不会有LOS格子）*/
	void RunFlowFieldPass(const FSector& Sector, const FIntegrationFieldArray& IntegrationFields, const FIntVector2& GoalLocalGridLocation, const FFlowFieldTarget& Target, FFlowFieldResult& OutResult) const;

	/** 把一个(DeltaX,DeltaY)连续方向量化成FFlowField的16方向之一(每22.5度一档，0度=+X方向，正向朝+Y轴旋转) */
	static FFlowField::EDirection QuantizeDirection(float DeltaX, float DeltaY);

	ASCE_INLINE static int32 GetLocalGridIndex(const FIntVector2& LocalGridLocation)
	{
		return LocalGridLocation.X + LocalGridLocation.Y * SECTOR_SIZE;
	}

	ASCE_INLINE static bool IsInsideSectorLocalBounds(const FIntVector2& LocalGridLocation)
	{
		return LocalGridLocation.X >= 0 && LocalGridLocation.X < SECTOR_SIZE && LocalGridLocation.Y >= 0 && LocalGridLocation.Y < SECTOR_SIZE;
	}

public:
	void CleanupFlowFieldCache();	
protected:
	
	TMap<FFlowFieldKey, FFlowFieldCacheEntry> FlowFieldCache;

	/** goal LOS 重算队列（升序 plan RawID）。 */
	TArray<uint32> GoalLOSRefreshQueue;

	/**
	 * Merged Path Plan（运行时按Handle查询的寻路方案，见FMergedPathPlan/FMergedPathPlanHandle）
	 */
protected:

	TMap<uint32, FMergedPathPlanEntry> PathPlans;
	uint32 NextPlanRawID = 1; // 0留作"无效Handle"

	FWorldMapData* WorldMapData;
};
