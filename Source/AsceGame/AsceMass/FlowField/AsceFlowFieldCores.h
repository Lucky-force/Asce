// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
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
	uint8 Cost = 0;
};
	
	
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
		return (LastCost < MAX_uint8 && NewCost == MAX_uint8) || (LastCost == MAX_uint8 && NewCost < MAX_uint8);
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

USTRUCT()
struct FCostFieldReplicated : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 Cost = 0;

	void PreReplicatedRemove(const struct FWorldMapData& InArraySerializer);
	void PostReplicatedAdd(const struct FWorldMapData& InArraySerializer);
	void PostReplicatedChange(const struct FWorldMapData& InArraySerializer);
};


USTRUCT()
struct FWorldMapData : public FFastArraySerializer
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
	UPROPERTY()
	TArray<FCostFieldReplicated> CostFields; 

#if WITH_EDITOR
	// 调试用：代价格子更新时广播（含GridIndex），Debugger增量更新纹理
	FOnWorldMapCostFieldChanged OnCostFieldChanged;

	// 调试用：Portal/Island结构变化时广播，Debugger全量重写纹理
	FOnWorldMapStructureChanged OnStructureChanged;
#endif

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FCostFieldReplicated, FWorldMapData>(CostFields, DeltaParms, *this);
	}

	void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize);
	void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);

	void UpdateWorld(float DeltaTime);	

	int MaxCostFieldProcessNumPerTick = 1024;
	int MaxSectorProcessNumPerTick = 128;
	int MaxIslandFieldProcessNumPerTick = 64; // IslandField重建要在Sector内部做一次波前扩散，比单纯弹出/校验一个Sector贵得多，单独给一个更保守的预算
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
	// 调试/运行时设置代价的统一入口（模拟复制路径，走 PendingUpdateCostFields → UpdateWorld 时间片处理）
	void SetCostField(uint32 GridIndex, uint8 NewCost)
	{
		if (GridIndex < static_cast<uint32>(CostFields.Num()))
		{
			CostFields[GridIndex].Cost = NewCost;
			MarkItemDirty(CostFields[GridIndex]);
			PendingUpdateCostFields.Add({GridIndex, NewCost});
		}
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
	int32 GetPortalCount() const { return Portals.Num(); }

private:
	friend FPortal;
	friend FSector;
	friend class UPathFinder;
};

template<>
struct TStructOpsTypeTraits<FWorldMapData> : public TStructOpsTypeTraitsBase2<FWorldMapData>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};