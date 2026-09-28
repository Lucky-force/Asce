// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "UObject/Object.h"
#include "AsceFlowFieldCores.generated.h"

// 流场模块统一日志类别
DECLARE_LOG_CATEGORY_EXTERN(LogFlowField, Log, All);
#if 0
#define ASCE_INLINE FORCEINLINE
#else
#define ASCE_INLINE FORCENOINLINE
#endif
/**
 * 
 */
#define SECTOR_SIZE 16
#define GRID_SIZE 100.f
struct FWorldMapData;

#if WITH_EDITOR
// 调试用委托：代价格子更新时广播（Debugger增量更新纹理像素）
DECLARE_MULTICAST_DELEGATE_OneParam(FOnWorldMapCostFieldChanged, uint32 /*GridIndex*/);
// 调试用委托：Portal/Island结构变化时广播（Debugger全量重写纹理）
DECLARE_MULTICAST_DELEGATE(FOnWorldMapStructureChanged);
#endif


struct FCostField
{
	// 数据布局（按你们的"最高位=墙/方向、低7位=代价或方向"方案改造）：
	//   bit7=0 → 可通行格, 低7位 = 代价 (1~127)
	//   bit7=1 → 墙格（低7位在旧版墙推流场方案里存过方向码，现在墙推改用格簇AABB，
	//             低7位此义已废弃，保留IsWall/GetPushDirection兼容读取旧数据）
	// 注意：旧版本墙用 Cost=255 (0b11111111, bit7=1, 低7位=127无效方向)——
	// IsWall()/GetPushDirection() 和旧数据兼容（255也判墙, 只是方向码无效, 读取方跳过即可）
	uint8 Cost = 0;

	ASCE_INLINE bool IsWall() const
	{
		return (Cost & 0b10000000) != 0;
	}

	ASCE_INLINE uint8 GetPushDirection() const
	{
		return Cost & 0b01111111;
	}
};

namespace AsceCostField
{
	/** "纯可通行"格的值（`SetGridPassable` 写的就是它）。放置校验要求目标格的 cost 必须等于它：
	 *  既是"没被地形墙/地形代价占用"，也是"没有已建成建筑占用"（建筑写 `BlockedCost`）。 */
	constexpr uint8 PlainPassableCost = 1;

	/** 阻挡值（地形 bake 与建筑占用统一用它）。 */
	constexpr uint8 BlockedCost = MAX_uint8;
}
	
	
struct FIntegrationField
{
	uint16 TotalIntegratedCost;

	enum class EIntegratedFlags : uint8
	{
		None = 0,
		LineOfSight = 1 << 0,
		ActiveWaveFront = 1 << 1,
		LOSWaveFrontBlocked = 1 << 2,
	};
	
	EIntegratedFlags IntegrationFlags;
};

struct FFlowField
{
	//X正方向为0，向Y轴正方向旋转的角度
	enum class EDirection : uint8
	{
		P_0     = 0b0000,
		P_22_5  = 0b0001,
		P_45    = 0b0010,
		P_67_5  = 0b0011,
		P_90    = 0b0100,
		P_112_5 = 0b0101,
		P_135   = 0b0110,
		P_157_5 = 0b0111,
		P_180   = 0b1000,
		P_202_5 = 0b1001,
		P_225   = 0b1010,
		P_247_5 = 0b1011,
		P_270   = 0b1100,
		P_292_5 = 0b1101,
		P_315   = 0b1110,
		P_337_5 = 0b1111,
	};

	enum class EFlowFieldFlags : uint8
	{
		HasLineOfSight = 1 << 4,
		Pathable = 1 << 5,
		Reserve1 = 1 << 6,
		Reserve2 = 1 << 7,
	};

	ASCE_INLINE EDirection GetDirection() const
	{
		return static_cast<EDirection>(RawData & 0b00001111);
	}

	ASCE_INLINE void SetDirection(EDirection direction)
	{
		RawData &= 0b11110000;
		RawData |= static_cast<uint8>(direction);
	}

	ASCE_INLINE bool HasLineOfSight() const
	{
		return RawData & static_cast<uint8>(EFlowFieldFlags::HasLineOfSight);
	}

	ASCE_INLINE bool Pathable() const
	{
		return RawData & static_cast<uint8>(EFlowFieldFlags::Pathable);
	}

	ASCE_INLINE void SetHasLineOfSight()
	{
		RawData |= static_cast<uint8>(EFlowFieldFlags::HasLineOfSight);
	}

	ASCE_INLINE void ClearHasLineOfSight()
	{
		RawData &= ~static_cast<uint8>(EFlowFieldFlags::HasLineOfSight);
	}

	ASCE_INLINE void SetPathable()
	{
		RawData |= static_cast<uint8>(EFlowFieldFlags::Pathable);
	}

	ASCE_INLINE void ClearPathable()
	{
		RawData &= ~static_cast<uint8>(EFlowFieldFlags::Pathable);
	}

	/** 把16方向枚举转换成单位长度的连续方向向量(与QuantizeDirection的角度约定一致：
	 * P_0=+X方向，往+Y轴方向旋转)，供运行时按格子查方向的消费端(entity移动)直接使用 */
	static const FVector2D& DirectionToVector(EDirection Direction)
	{
		static const FVector2D Table[16] = {
			FVector2D(1.f, 0.f),          FVector2D(0.923880f, 0.382683f),
			FVector2D(0.707107f, 0.707107f), FVector2D(0.382683f, 0.923880f),
			FVector2D(0.f, 1.f),          FVector2D(-0.382683f, 0.923880f),
			FVector2D(-0.707107f, 0.707107f), FVector2D(-0.923880f, 0.382683f),
			FVector2D(-1.f, 0.f),         FVector2D(-0.923880f, -0.382683f),
			FVector2D(-0.707107f, -0.707107f), FVector2D(-0.382683f, -0.923880f),
			FVector2D(0.f, -1.f),         FVector2D(0.382683f, -0.923880f),
			FVector2D(0.707107f, -0.707107f), FVector2D(0.923880f, -0.382683f),
		};
		return Table[static_cast<uint8>(Direction) & 0b1111];
	}
	
private:
	uint8 RawData = 0;
};

struct FIslandField
{
	FIslandField()
	{
		IslandId = 0;
	}

	FIslandField(uint8 InIslandId)
	{
		IslandId = InIslandId;
	}
	
	bool operator==(const FIslandField& Other) const
	{
		return Other.IslandId == IslandId;
	}

	void operator=(const FIslandField& Other)
	{
		IslandId = Other.IslandId;
	}

	void Reset()
	{
		IslandId = 0;
	}

	void SetValue(uint8 Value)
	{
		IslandId = Value;
	}
	
	uint8 IslandId;
};

struct FSector
{
	struct FSectorID
	{
		FSectorID()
		{
			RawID = MAX_uint16;
			Reserved = 0;
			TimeStamp = 0;
		};

		FSectorID(uint16 Id)
		{
			RawID = Id;
			Reserved = 0;
			TimeStamp = 0;
		}

		ASCE_INLINE bool IsValid() const
		{
			return RawID != MAX_uint16;
		}

		bool operator==(const FSectorID& Other) const
		{
			/**
			 *只要RawID对的上就认为SectorID是相等的，TimeStamp是用来判断Sector是否在时间上相等，
			 *通常不需要进行这种判断（否则Sector变化所有持有SectorID的地方都直接失效了，允许它们用旧的SectorID在空间上查询Sector，随后使用TimeStamp进行判断）
			*/
			return RawID == Other.RawID;
		}

		bool operator<(const FSectorID& Other) const
		{
			return RawID < Other.RawID;
		}

		bool operator>(const FSectorID& Other) const
		{
			return RawID > Other.RawID;
		}

		bool operator<=(const FSectorID& Other) const
		{
			return RawID <= Other.RawID;
		}

		bool operator>=(const FSectorID& Other) const
		{
			return RawID >= Other.RawID;
		}

		// hash只参与"集合/字典查找"，语义与operator==一致（只比较RawID，Timestamp不参与hash）——
		// 否则同一个Sector在不同时间线会导致TSet去重不了（PendingWallPushSectors等按空间扇区判断的场景就漏了）
		friend ASCE_INLINE uint32 GetTypeHash(const FSectorID& SectorID)
		{
			return static_cast<uint32>(SectorID.RawID);
		}
		
		uint16 RawID;
		uint16 Reserved;
		//TimeStamp代表持有该ID的Sector当前的时间线，如果外部持有SectorID的地方发现SectorID和Sector的时间线不一致，说明Sector发生了变化，此时可以决定进行何种操作来处理这种变化
		uint32 TimeStamp;
	};

	struct FSectorFlags
	{
		bool UpEdgeDirty : 1;
		bool RightEdgeDirty : 1;
		bool DownEdgeDirty : 1;
		bool LeftEdgeDirty : 1;
		bool Reserve0 : 1;
		bool Reserve1 : 1;
		bool Reserve2 : 1;
		bool Reserve3 : 1;

		ASCE_INLINE bool IsEdgeDirty() const
		{
			return UpEdgeDirty || RightEdgeDirty || DownEdgeDirty || LeftEdgeDirty;
		}
	};

	ASCE_INLINE bool IsGridBlockStateChanged(uint8 LastCost, uint8 NewCost)
	{
		if (LastCost == 0)
		{
			return true;
		}
		// 墙状态变化 = 该格 IsWall 标志翻转（不管具体代价/方向值怎么变）
		const bool LastWall = (LastCost & 0b10000000) != 0;
		const bool NewWall  = (NewCost  & 0b10000000) != 0;
		return LastWall != NewWall;
	}
	
	ASCE_INLINE bool UpdateCostField(int Index, uint8 NewCost)
	{
		bool Result = false;
		if (Index >= CostFields.Num() || Index < 0)
		{
			return Result;
		}
		Result = IsGridBlockStateChanged(CostFields[Index].Cost, NewCost);
		CostFields[Index] = {NewCost};
		return Result;
	}

	ASCE_INLINE int GetUpEdgeGridStartIndex() const
	{
		return CostFields.Num() - SECTOR_SIZE;
	}
	ASCE_INLINE int GetRightEdgeGridStartIndex() const
	{
		return SECTOR_SIZE - 1;
	}
	ASCE_INLINE int GetDownEdgeGridStartIndex() const
	{
		return 0;
	}
	ASCE_INLINE int GetLeftEdgeGridStartIndex() const
	{
		return 0;
	}

	ASCE_INLINE bool CheckTimeStamp(FSectorID OtherSectorID) const
	{
		if (OtherSectorID != SectorID)
		{
			return false;
		}
		ensureAlways(OtherSectorID.TimeStamp <= SectorID.TimeStamp);
		return OtherSectorID.TimeStamp == SectorID.TimeStamp;
	}
	
	static const FSectorID InvalidSectorID;
	static const FIslandField InvalidIslandField;

	FSectorID SectorID;
	TArray<FCostField, TInlineAllocator<SECTOR_SIZE * SECTOR_SIZE>> CostFields;
	TArray<FIslandField, TInlineAllocator<SECTOR_SIZE * SECTOR_SIZE>> IslandFields;
};

struct FPortal
{
	struct FPortalIndex
	{
		uint32 RawIndex;

		FPortalIndex()
		{
			RawIndex = MAX_uint32;
		};

		FPortalIndex(uint32 Index)
		{
			RawIndex = Index;
		}

		ASCE_INLINE bool IsValid() const
		{
			return RawIndex != MAX_uint32;
		}

		bool operator==(const FPortalIndex& Other) const
		{
			return RawIndex == Other.RawIndex;
		}

		bool operator<(const FPortalIndex& Other) const
		{
			return RawIndex < Other.RawIndex;
		}

		bool operator>(const FPortalIndex& Other) const
		{
			return RawIndex > Other.RawIndex;
		}

		bool operator<=(const FPortalIndex& Other) const
		{
			return RawIndex <= Other.RawIndex;
		}

		bool operator>=(const FPortalIndex& Other) const
		{
			return RawIndex >= Other.RawIndex;
		}

		void operator++()
		{
			++RawIndex;
		}

		void operator--()
		{
			--RawIndex;
		}

		friend ASCE_INLINE uint32 GetTypeHash(const FPortal::FPortalIndex& Index)
		{
			return GetTypeHash(Index.RawIndex);
		}
	};

	struct FPortalFlags
	{
		uint8 IsDirty : 1;
		uint8 PendingRemove : 1;
	};

	enum class EPortalType : uint8
	{
		Left = 0b00000000,
		Up = 0b01000000,
		Down = 0b10000000,
		Right = 0b11000000,
		Invalid = 0b11110000,
	};

	ASCE_INLINE EPortalType GetPortalType() const
	{
		return static_cast<EPortalType>(RawFlags & 0b11110000);
	}

	ASCE_INLINE bool IsDirty() const
	{
		return RawFlags & 0b00000001;
	}

	ASCE_INLINE void SetPortalType(EPortalType PortalType)
	{
		RawFlags &= 0b00001111;
		RawFlags |= static_cast<uint8>(PortalType);
	}

	ASCE_INLINE void MarkDirty()
	{
		RawFlags |= 0b00000001;
	}

	ASCE_INLINE void MarkPendingRemove()
	{
		RawFlags |= 0b00000010;
	}

	ASCE_INLINE bool IsPendingRemove() const
	{
		return RawFlags & 0b00000010;
	}
	
	ASCE_INLINE bool IsLinkable(const FPortal& Other) const
	{
		if (PortalBitMap != Other.PortalBitMap)
		{
			return false;
		}
		return static_cast<uint8>(GetPortalType()) + static_cast<uint8>(Other.GetPortalType()) == 0b11000000;
	}

	ASCE_INLINE bool IsValid() const
	{
		return PortalBitMap != 0;
	}

	//将一个完整的记录了一条边的Bitmap提取为多个Portal的Bitmap
	template <typename AllocatorType>
	static void ExtractPortalBitmaps(uint16 TotalPortalBitmap, TArray<uint16, AllocatorType>& ExtractedBitmaps)
	{
		bool LastBitIsValid = false;
		uint16 CurrentPortalBitmap = 0;
		for (int i = 0; i < 16; i++)
		{
			if (TotalPortalBitmap & 1 << (15 - i))
			{
				if (!LastBitIsValid)
				{
					LastBitIsValid = true;
					CurrentPortalBitmap = 0;
				}
				CurrentPortalBitmap |= 1 << (15 - i);
			}
			else
			{
				LastBitIsValid = false;
				if (CurrentPortalBitmap)
				{
					ExtractedBitmaps.Add(CurrentPortalBitmap);
					CurrentPortalBitmap = 0;
				}
			}
		}

		if (CurrentPortalBitmap)
		{
			ExtractedBitmaps.Add(CurrentPortalBitmap);
		}
	}
	

	FSector::FSectorID GetLinkToSectorID(const FWorldMapData& WorldMapData) const;
	
	const FPortal& GetLinkToPortal(const FWorldMapData& WorldMapData) const;

	FPortalIndex GetLinkToPortalIndex(const FWorldMapData& WorldMapData) const;
	
	uint16 PortalBitMap;
	FSector::FSectorID OwingSectorID;
	FIslandField OwingIslandField;
	uint8 RawFlags;
};

// 墙格簇：由相邻的墙格合并成的一个尽可能大的轴对齐矩形（世界坐标AABB）。
// 墙的碰撞解算对簇做一次圆-AABB接触即可（法线=圆心到簇最近点，始终朝簇外，
// 不会像"逐格解算"那样在相邻格交界处把法线算歪、出现朝墙内推的错误）。
// 两端（Server/Client）基于复制的CostFields本地确定性重建，不是复制数据，不参与网络传输。
struct FWallCluster
{
	FVector2D Min = FVector2D::ZeroVector;
	FVector2D Max = FVector2D::ZeroVector;

	bool Contains(const FVector2D& Point) const
	{
		return Point.X >= Min.X && Point.X <= Max.X && Point.Y >= Min.Y && Point.Y <= Max.Y;
	}
};

USTRUCT()
struct FWorldMapData
{
	GENERATED_BODY()
public:
	void InitWorldMapData(uint8 InSectorCountX, uint8 InSectorCountY);
	
	//世界坐标到网格坐标转换
	ASCE_INLINE static FIntVector2 WorldLocationToGridLocation(const FVector2D& WorldLocation)
	{
		return FIntVector2(FMath::FloorToInt32(WorldLocation.X / GRID_SIZE), FMath::FloorToInt32(WorldLocation.Y / GRID_SIZE));
	};

	//网格坐标到扇区ID转换
	ASCE_INLINE FSector::FSectorID GridLocationToSectorId(const FIntVector2& GridLocation) const
	{
		if (GridLocation.X < 0 || GridLocation.Y < 0 || static_cast<uint32>(GridLocation.X) >= GridCountX || static_cast<uint32>(GridLocation.Y) >= GridCountY)
		{
			return FSector::InvalidSectorID;
		}
		int YId = GridLocation.Y / SECTOR_SIZE;
		int XId = GridLocation.X / SECTOR_SIZE;
		return FSector::FSectorID(XId + SectorCountX * YId);
	}

	//世界坐标到扇区ID转换
	ASCE_INLINE FSector::FSectorID WorldLocationToSectorId(const FVector2D& WorldLocation) const
	{
		return GridLocationToSectorId(WorldLocationToGridLocation(WorldLocation));
	}

	//扇区ID转扇区坐标（扇区左下角的网格坐标）
	ASCE_INLINE FIntVector2 GetSectorLocation(FSector::FSectorID SectorID) const
	{
		return FIntVector2(SectorID.RawID % SectorCountX, SectorID.RawID / SectorCountX) * SECTOR_SIZE;
	}

	//网格坐标转其在扇区中的索引
	ASCE_INLINE uint32 GridLocationToGridIndexInSector(const FIntVector2& GridLocation) const
	{
		FIntVector2 SectorLocation = GetSectorLocation(GridLocationToSectorId(GridLocation));
		return GridLocation.X - SectorLocation.X + (GridLocation.Y - SectorLocation.Y) * SECTOR_SIZE;
	};

	ASCE_INLINE FIntVector2 GridIndexToGridLocation(const uint32 GridIndex) const
	{
		return FIntVector2(GridIndex % GridCountX, GridIndex / GridCountX);
	}

	ASCE_INLINE uint32 GridLocationToGridIndex(const FIntVector2& GridLocation) const
	{
		return GridLocation.Y * GridCountX + GridLocation.X;
	}

	ASCE_INLINE const FSector& GetSectorData(FSector::FSectorID SectorID) const
	{
		if (SectorID.IsValid() && SectorID.RawID < SectorIDToIndexMap.Num())
		{
			int SectorIndex = SectorIDToIndexMap[SectorID.RawID];
			if (SectorIndex < SectorDataList.Num())
			{
				return SectorDataList[SectorIndex];
			}
		}
		return SectorDataList[0];
	}

	ASCE_INLINE const FPortal& GetPortal(FPortal::FPortalIndex PortalIndex) const
	{
		if (PortalIndex.IsValid() && PortalIndex.RawIndex < static_cast<uint32>(Portals.Num()))
		{
			return Portals[PortalIndex.RawIndex];
		}

		return InvalidPortal;
	}

	ASCE_INLINE uint16 GetSectorIndex(FSector::FSectorID SectorID)
	{
		if (SectorID.IsValid() && SectorID.RawID < SectorIDToIndexMap.Num())
		{
			return SectorIDToIndexMap[SectorID.RawID];
		}
		return MAX_uint16;
	}

	FPortal::FPortalIndex GetPortalIndexByGridIndex(const uint32 GridIndex) const
	{
		return GetPortalIndexByGridIndex(GridIndex, nullptr);
	};
	FPortal::FPortalIndex GetPortalIndexByGridIndex(const uint32 GridIndex, bool* IsPortalGrid) const;

	bool IsEdgeGrid(const uint32 GridIndex) const;
	
	void FindPortalIndicesInSectorIsland(FSector::FSectorID SectorID, FIslandField IslandField, TArray<FPortal::FPortalIndex>& PortalIndices) const;

	void FindPortalIndicesInSector(FSector::FSectorID SectorID, TArray<FPortal::FPortalIndex>& PortalIndices) const;

	//废弃不使用，二分查找还是有点慢了
	template <typename PredicateType, typename AllocatorType>
	void FindPortalIndicesInSector(FSector::FSectorID SectorID, PredicateType FilterFunc, TArray<FPortal::FPortalIndex, AllocatorType>& PortalIndices) const
	{
		PortalIndices.Reset();

		const int32 StartIndex = Algo::LowerBoundBy(Portals, SectorID, &FPortal::OwingSectorID);
		for (int32 Index = StartIndex; Index < Portals.Num(); ++Index)
		{
			const FPortal& Portal = Portals[Index];
			if (Portal.OwingSectorID != SectorID)
			{
				break;
			}
			if (FilterFunc(Portal))
			{
				PortalIndices.Add(static_cast<FPortal::FPortalIndex>(Index));
			}
		}
	}

	template <typename PredicateType, typename AllocatorType>
	void FindPortalIndicesInSectorFast(FSector::FSectorID SectorID, PredicateType FilterFunc, TArray<FPortal::FPortalIndex, AllocatorType>& PortalIndices) const
	{
		PortalIndices.Reset();
		FPortal::FPortalIndex Start, End;
		GetSectorPortalIndicesRange(SectorID, Start, End);
		for (uint32 Index = Start.RawIndex; Index < End.RawIndex; ++Index)
		{
			const FPortal& Portal = Portals[Index];
			if (FilterFunc(Portal))
			{
				PortalIndices.Add(FPortal::FPortalIndex(Index));
			}
		}
	}

	static FPortal::EPortalType GetPossiblePortalType(FIntVector2 GridLocation);
	static int GetEdgeGridStartIndex(const FSector& TargetSector, const FPortal::EPortalType EdgeType);

public:
	// ── 代价场（本地 dense；**不参与复制**，内容由 `AAsceCostFieldChunkActor` 分块复制到客户端）──
	TArray<FCostField> CostFields;

	/** 是否走"分帧复制队列"：只有**有客户端的权威端**（DS / ListenServer）才开；
	 *  Standalone / 客户端不需要（本地 dense 已即时写，无网络动作）。 */
	bool bUsePacedReplication = false;

	/** 分帧队列每帧最多处理多少格（把读档/大范围编辑的洪峰摊到多帧） */
	int32 ReplicatedUpdatesPerFrame = 256;

	// 分帧队列：待分发的格号（同格合并；顺序按首次入队位置，FIFO）
	TSet<uint32> PendingReplicatedCells;
	TArray<uint32> PendingReplicatedOrder;
	int32 PendingReplicatedHead = 0;

#if WITH_EDITOR
	// 调试用：代价格子更新时广播（含GridIndex），Debugger增量更新纹理
	FOnWorldMapCostFieldChanged OnCostFieldChanged;

	// 调试用：Portal/Island结构变化时广播，Debugger全量重写纹理
	FOnWorldMapStructureChanged OnStructureChanged;
#endif

	void UpdateWorld(float DeltaTime);	

	// 重建墙格簇（FWallCluster AABB）：把连通墙格合并成尽可能大的轴对齐矩形簇。
	// 替代旧"墙推流场方向"方案——墙的碰撞解算（圆-簇接触）只需要簇AABB，
	// 不再需要每格存方向码。调度复用PendingWallPushSectors（墙变化时入队→UpdateWorld统一处理），
	// 两端（Server/Client）都跑：簇数据不复制，由两端基于复制的CostFields本地确定性重建。
	void RebuildWallClusters();

	int MaxCostFieldProcessNumPerTick = 1024;
	int MaxSectorProcessNumPerTick = 128;
	int MaxIslandFieldProcessNumPerTick = 64; // IslandField重建要在Sector内部做一次波前扩散，比单纯弹出/校验一个Sector贵得多，单独给一个更保守的预算

	// ── 墙格簇重建的调度状态 ──
	// 变墙(状态翻转)时OnCostFieldUpdate把该格所属扇区加入，RebuildWallClusters统一处理：
	// 集合类型保证去重（同一帧多个格子在同一个扇区只处理一次）
	TSet<FSector::FSectorID> PendingWallPushSectors;

	// 处理批次内"某墙格是否已并入某个簇"的全局标记（1 bit/格）。
	// 用UE的TBitArray按bit存储（1 bit/格而不是1字节/格），按全局GridIndex索引，
	// 簇合并时已归属的格子置位，后续遍历到即跳过，不会重复归簇。
	// 批次开始一次性清空（Init(false, GridNum)），批内持续有效。
	TBitArray<> WallGridBitmap; // 大小 = GridCountX*GridCountY，0=未入簇，1=已并入某簇

	// 墙格簇列表（世界坐标AABB，两端本地重建，非复制数据，只读访问并行安全）
	TArray<FWallCluster> WallClusters;

	// 格→簇索引查找表（大小=GridCountX*GridCountY的格子总数）：
	// 每个元素 = 该格所属簇在WallClusters里的索引（-1=不属于任何墙簇）。
	// RebuildWallClusters时顺手填充（簇覆盖的所有格子写入索引），
	// Solver按"单位覆盖的格子范围"查表O(1)访问，避免遍历全部簇（簇多时）。只读访问并行安全。
	TArray<int32> GridIndexToCluster;
protected:

	void ProcessCostFields(float DeltaTime);
	void ProcessSectors(float DeltaTime);
	void ProcessPortals(float DeltaTime);
	void ProcessIslandFields(float DeltaTime);
	
	void OnPortalAdded(FPortal& Portal);
	void OnPortalRemoved(FPortal& Portal);

	/** 唯一更新代价场的入口*/
	void OnCostFieldUpdate(uint32 GridIndex, uint8 NewCost);

	/** 代价场更新后标脏Sector，随后此处进行处理*/
	bool OnSectorProcess(FSector::FSectorID SectorID);

	/** 处理某条边上的传送门问题*/
	void ProcessSectorPortalsOnEdge(const FSector& Sector);

	/** 重新计算Sector内部的IslandField*/
	void ProcessIslandFieldForSector(FSector& Sector);

	void UpdateSectorIDToPortalStartIndexMap();

	/** 如果这个Sector现在还指向共享的0号PlainSectorData，就先深拷贝出一份独占数据(CostFields/IslandFields内容原样复制，
	 * SectorID换成真实值)，再让SectorIDToIndexMap对应位置指向新条目；返回的是SectorDataList里的索引而不是引用——
	 * 因为这里的SectorDataList.Add()是可能真的触发底层数组扩容的(为了保留PlainSectorData"没写过的Sector不占内存"的
	 * 惰性优势，这里不会像"预留最大容量"那样规避扩容)。一旦扩容，所有此前拿到手的FSector&引用都会失效。
	 * 调用规则：
	 *  - 如果一次函数调用只会涉及一个Sector，用完引用立刻结束(不会再调用任何可能触发别的Sector COW的函数)，
	 *    直接用下面的GetMutableSectorData便利接口就够了；
	 *  - 如果一次函数调用需要同时处理多个Sector(比如自己+邻居)，必须先对每一个都调用一次GetOrCreateMutableSectorIndex
	 *    拿到索引，确认所有可能触发COW的调用都做完之后，再统一从SectorDataList[Index]取引用使用——引用只在
	 *    "拿到之后、下一次调用GetOrCreateMutableSectorIndex/GetMutableSectorData之前"这段范围内有效，不能跨越缓存 */
	ASCE_INLINE uint16 GetOrCreateMutableSectorIndex(FSector::FSectorID SectorID)
	{
		if (!SectorID.IsValid() || SectorID.RawID >= SectorIDToIndexMap.Num())
		{
			return 0;
		}

		uint16& SectorIndexRef = SectorIDToIndexMap[SectorID.RawID];
		if (SectorIndexRef == 0)
		{
			FSector NewSectorData = SectorDataList[0]; // 拷贝Plain的CostFields/IslandFields内容
			NewSectorData.SectorID = FSector::FSectorID(SectorID.RawID);
			SectorIndexRef = static_cast<uint16>(SectorDataList.Add(MoveTemp(NewSectorData)));
		}
		return SectorIndexRef;
	}

	/** 便利接口：仅在"这次调用只会涉及一个Sector"的场景下使用(比如OnCostFieldUpdate/ProcessIslandFields)。
	 * 一个函数里要连续处理多个Sector时，改用GetOrCreateMutableSectorIndex，规则见其注释 */
	ASCE_INLINE FSector& GetMutableSectorData(FSector::FSectorID SectorID)
	{
		return SectorDataList[GetOrCreateMutableSectorIndex(SectorID)];
	}

	ASCE_INLINE FPortal& GetMutablePortal(FPortal::FPortalIndex PortalIndex)
	{
		if (PortalIndex.IsValid() && PortalIndex.RawIndex < static_cast<uint32>(Portals.Num()))
		{
			return Portals[PortalIndex.RawIndex];
		}

		return InvalidPortal;
	}

	ASCE_INLINE bool IsSectorDirty(const FSector::FSectorID SectorID)
	{
		if (SectorID.RawID < SectorDirtyList.Num())
		{
			return SectorDirtyList[SectorID.RawID];
		}
		return false;
	}

	ASCE_INLINE bool IsSectorEdgeDirty(const FSector::FSectorID SectorID, const FPortal::EPortalType PortalType)
	{
		if (SectorID.RawID >= SectorFlagsList.Num())
		{
			return false;
		}
		
		FSector::FSectorFlags& SectorFlags = SectorFlagsList[SectorID.RawID];
		
		switch (PortalType)
		{
			case FPortal::EPortalType::Up:
				{
					return SectorFlags.UpEdgeDirty;
				};

			case FPortal::EPortalType::Down:
				{
					return SectorFlags.DownEdgeDirty;	
				}
			case FPortal::EPortalType::Left:
				{
					return SectorFlags.LeftEdgeDirty;
				}
			case FPortal::EPortalType::Right:
				{
					return SectorFlags.RightEdgeDirty;
				}
			default: return false;
		}	
	}

	ASCE_INLINE void MarkSectorEdgeDirty(const FSector::FSectorID SectorID, const FPortal::EPortalType PortalType)
	{
		if (SectorID.RawID >= SectorFlagsList.Num())
		{
			return;
		}
		
		FSector::FSectorFlags& SectorFlags = SectorFlagsList[SectorID.RawID];
		switch (PortalType)
		{
		case FPortal::EPortalType::Up:
			{
				SectorFlags.UpEdgeDirty = true;
			}; break;

		case FPortal::EPortalType::Down:
			{
				SectorFlags.DownEdgeDirty = true;	
			}; break;
		case FPortal::EPortalType::Left:
			{
				SectorFlags.LeftEdgeDirty = true;
			}; break;
		case FPortal::EPortalType::Right:
			{
				SectorFlags.RightEdgeDirty = true;
			}; break;
		default: return;
		}	
	}

	ASCE_INLINE void MarkSectorDirty(const FSector::FSectorID SectorID)
	{
		if (SectorID.RawID >= SectorDirtyList.Num())
		{
			return;
		}
		SectorDirtyList[SectorID.RawID] = true;
	}

	ASCE_INLINE void GetSectorPortalIndicesRange(FSector::FSectorID SectorID, FPortal::FPortalIndex& OutStart, FPortal::FPortalIndex& OutEnd) const
	{
		if (!SectorID.IsValid() || SectorID.RawID + 1 >= SectorIDToPortalStartIndexMap.Num())
		{
			OutStart = OutEnd = 0;
			return;
		}
		OutStart = SectorIDToPortalStartIndexMap[SectorID.RawID];
		OutEnd = SectorIDToPortalStartIndexMap[SectorID.RawID + 1];
	}

protected:
	uint8 SectorCountX = 0;
	uint8 SectorCountY = 0;
	uint32 GridCountX = 0;
	uint32 GridCountY = 0;
	TArray<uint16> SectorIDToIndexMap;
	TArray<FSector> SectorDataList;
	TArray<FPortal::FPortalIndex> SectorIDToPortalStartIndexMap;
	TArray<FSector::FSectorFlags> SectorFlagsList;
	TBitArray<> SectorDirtyList;
	TArray<FPortal> Portals;

	static FPortal InvalidPortal;

	struct FPendingUpdateCostField
	{
		uint32 GridIndex;
		uint8 NewCost;
	};

	//当前需要处理的CostFields
	TArray<FPendingUpdateCostField> PendingUpdateCostFields;

	//当前需要处理的Sectors
	TArray<FSector::FSectorID> PendingProcessSectors;

	//从PendingProcessSectors中取出的，用于这一帧进行IslandField重新处理的Sectors
	TArray<FSector::FSectorID> PendingProcessIslandSectorsThisTick;

	//要添加的Portals
	TArray<FPortal> PendingAddPortalsThisTick;

	//持久复用的归并缓冲区
	TArray<FPortal> PortalMergeScratchBuffer;

	//这一帧是否有Portal被标记待删除
	bool bHasPendingPortalRemovals = false;
	
	
	/** Debugger */
public:
	// 唯一更新代价场的入口：本地 dense **即时写**（寻路/碰撞/放置校验不等），
	// 复制分发按模式走：有客户端的权威端进分帧队列（由 ReplicatorActor drain 到分块 actor）；其余端无网络动作
	void SetCostField(uint32 GridIndex, uint8 NewCost)
	{
		if (GridIndex < static_cast<uint32>(CostFields.Num()))
		{
			CostFields[GridIndex].Cost = NewCost;
			PendingUpdateCostFields.Add({GridIndex, NewCost});
			QueueReplicatedCostField(GridIndex);
		}
	}

	/** 客户端收到分块复制的格：写回本地 dense + 进本地处理管线（ProcessCostFields → 扇区/墙簇重建） */
	void ApplyReplicatedCostCell(uint32 GridIndex, uint8 NewCost)
	{
		if (CostFields.IsValidIndex(static_cast<int32>(GridIndex)))
		{
			CostFields[GridIndex].Cost = NewCost;
			PendingUpdateCostFields.Add({GridIndex, NewCost});
		}
	}

	/** 复制分发：有客户端的权威端入队列（同格合并）；其余端什么都不做（本地 dense 已即时写好） */
	void QueueReplicatedCostField(uint32 GridIndex)
	{
		if (!bUsePacedReplication)
		{
			return;
		}
		if (PendingReplicatedCells.Contains(GridIndex))
		{
			return;   // 同格合并：已排队；drain 时按**最新值**分发
		}
		PendingReplicatedCells.Add(GridIndex);
		PendingReplicatedOrder.Add(GridIndex);
	}

	/** 每帧 drain：从队列头部按预算弹出并把格号交给路由（有客户端的权威端，由 ReplicatorActor 的 Tick 驱动）；
	 *  路由负责把该格分发到对应 `AAsceCostFieldChunkActor`（值从本地 dense 读取） */
	template<typename FuncType>
	void DrainReplicatedCostUpdates(FuncType&& ApplyFn)
	{
		if (!bUsePacedReplication)
		{
			return;
		}
		const int32 Remaining = PendingReplicatedOrder.Num() - PendingReplicatedHead;
		if (Remaining <= 0)
		{
			PendingReplicatedCells.Reset();
			PendingReplicatedOrder.Reset();
			PendingReplicatedHead = 0;
			return;
		}

		const int32 Count = FMath::Min(FMath::Max(1, ReplicatedUpdatesPerFrame), Remaining);
		for (int32 i = 0; i < Count; ++i)
		{
			const uint32 Index = PendingReplicatedOrder[PendingReplicatedHead + i];
			if (CostFields.IsValidIndex(static_cast<int32>(Index)))
			{
				ApplyFn(Index);
			}
			PendingReplicatedCells.Remove(Index);
		}
		PendingReplicatedHead += Count;

		if (PendingReplicatedHead >= PendingReplicatedOrder.Num())
		{
			PendingReplicatedCells.Reset();
			PendingReplicatedOrder.Reset();
			PendingReplicatedHead = 0;
		}
		else if (PendingReplicatedHead > 1024)
		{
			// 摊销头部滑窗（避免每帧 RemoveAt(0) 的 memmove）
			PendingReplicatedOrder.RemoveAt(0, PendingReplicatedHead, EAllowShrinking::No);
			PendingReplicatedHead = 0;
		}
	}

	/** 清空未分发的复制队列（建图预填后丢弃冗余入队；重 init 时用） */
	void ResetReplicatedCostQueue()
	{
		PendingReplicatedCells.Reset();
		PendingReplicatedOrder.Reset();
		PendingReplicatedHead = 0;
	}

	// 置某格为普通可通行（代价1）：等价 SetCostField(Index, 1)
	ASCE_INLINE void SetGridPassable(uint32 GridIndex) { SetCostField(GridIndex, 1); }

	// 置某格为墙，方向（推力流场方向码）由调用方给出——默认 0b01111111（无效方向占位，RebuildWallPushFlow 补全）
	ASCE_INLINE void SetGridWall(uint32 GridIndex, uint8 PushDirection = 0b01111111)
	{
		SetCostField(GridIndex, 0b10000000 | (PushDirection & 0b01111111));
	}

	// 世界数据是否有未处理完的变更（Debugger 用它判断是否需要重绘）
	bool HasPendingWorldUpdates() const
	{
		return PendingUpdateCostFields.Num() > 0
			|| PendingProcessSectors.Num() > 0
			|| PendingProcessIslandSectorsThisTick.Num() > 0
			|| PendingAddPortalsThisTick.Num() > 0
			|| bHasPendingPortalRemovals;
	}

	int32 GetSectorCountX() const { return SectorCountX; }
	int32 GetSectorCountY() const { return SectorCountY; }
	int32 GetGridCountX() const  { return static_cast<int32>(GridCountX); }
	int32 GetGridCountY() const  { return static_cast<int32>(GridCountY); }

	// ── 逐格查询（建筑放置校验等用；越界一律按"阻挡"处理）──

	/** 逻辑格是否被阻挡（地形墙 / 已建成建筑）。 */
	ASCE_INLINE bool IsCellBlocked(int32 X, int32 Y) const
	{
		const int32 Index = X + Y * static_cast<int32>(GridCountX);
		return !CostFields.IsValidIndex(Index) || CostFields[Index].IsWall();
	}

	/** 逻辑格是否是"纯可通行"（cost == `AsceCostField::PlainPassableCost`）：放置要求它是。 */
	ASCE_INLINE bool IsCellCleanPassable(int32 X, int32 Y) const
	{
		const int32 Index = X + Y * static_cast<int32>(GridCountX);
		return CostFields.IsValidIndex(Index) && CostFields[Index].Cost == AsceCostField::PlainPassableCost;
	}
	int32 GetPortalCount() const { return Portals.Num(); }

private:
	friend FPortal;
	friend FSector;
	friend class UPathFinder;
};
