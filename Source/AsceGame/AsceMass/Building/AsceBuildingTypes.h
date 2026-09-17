// AsceBuildingTypes.h
#pragma once

#include "CoreMinimal.h"
#include "MassEntityHandle.h"   // FMassEntityHandle（5.7 起 MassEntityTypes.h 不再带它）
#include "AsceBuildingTypes.generated.h"

class UAsceEtherBuildingConfig;
class AAsceBuildingPlacementActor;

/**
 * 一次"放置"的完整描述：摆放 Actor / 玩家建造请求 / 存档 都用它。
 * 坐标与 `FAsceGridFootprintFragment::OriginGridLocation` 同一套（逻辑格 = costfield 格 = 100cm）。
 */
USTRUCT(BlueprintType)
struct FAsceBuildingPlacement
{
	GENERATED_BODY()

	/** footprint **未旋转**的最小角（逻辑格）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	FIntPoint OriginGridLocation = FIntPoint::ZeroValue;

	/** 未旋转的占格尺寸（来自建筑配置）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	FIntPoint Size = FIntPoint(1, 1);

	/** 绕 footprint 中心**顺时针** 90° 的次数（0~3）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "0", ClampMax = "3"))
	int32 Rotation = 0;

	/** 建筑配置（蓝图子类 CDO）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	TSubclassOf<UAsceEtherBuildingConfig> BuildingConfig;

	/** 登记用建筑 id（None ⇒ 取配置类名，与 `UAsceEtherBuildingTrait` 一致）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	FName BuildingDefId;

	/** 归属阵营（写进 `FAsceTeamFragment::TeamKey`）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	int32 TeamKey = 0;

	/** 由"配置 + 中心格 + 朝向"生成摆放（摆放 Actor / 调试器共用；`Size` 取自配置的 SizeX/SizeY）。 */
	static FAsceBuildingPlacement MakeFromConfigCenter(
		TSubclassOf<UAsceEtherBuildingConfig> InConfig,
		const FIntPoint& CenterCell,
		int32 InRotation = 0,
		int32 InTeamKey = 0,
		FName InBuildingDefId = NAME_None);

	/** footprint 中心（逻辑格坐标，可为半格）。 */
	FORCEINLINE FVector2D GetCenterCell() const
	{
		const FIntPoint S(FMath::Max(1, Size.X), FMath::Max(1, Size.Y));
		return FVector2D(
			OriginGridLocation.X + (S.X - 1) * 0.5,
			OriginGridLocation.Y + (S.Y - 1) * 0.5);
	}

	/** 占用的逻辑格 AABB（按 `Rotation` 绕中心旋转后；与建筑层登记口径一致）。 */
	FORCEINLINE void GetOccupiedBounds(FIntPoint& OutMin, FIntPoint& OutMax) const
	{
		const FIntPoint S(FMath::Max(1, Size.X), FMath::Max(1, Size.Y));
		// 2 倍中心（避免半格取整误差）：2*Origin + S - 1
		const FIntPoint Center2(2 * OriginGridLocation.X + S.X - 1, 2 * OriginGridLocation.Y + S.Y - 1);
		const bool bSwap = (Rotation == 1 || Rotation == 3);
		const FIntPoint Extent = bSwap ? FIntPoint(S.Y, S.X) : S;
		OutMin = FIntPoint(
			FMath::FloorToInt((Center2.X - (Extent.X - 1)) * 0.5),
			FMath::FloorToInt((Center2.Y - (Extent.Y - 1)) * 0.5));
		OutMax = FIntPoint(OutMin.X + Extent.X - 1, OutMin.Y + Extent.Y - 1);
	}
};

/** 预摆项：摆放 Actor 已报到、等引导阶段落地。 */
USTRUCT()
struct FAscePendingPlacement
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TWeakObjectPtr<AAsceBuildingPlacementActor> Actor;

	UPROPERTY(Transient)
	FAsceBuildingPlacement Placement;
};

/** 已建成建筑（Entity ↔ 摆放数据；拆除/存档用）。 */
USTRUCT()
struct FAsceBuiltBuilding
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FMassEntityHandle Entity;

	UPROPERTY(Transient)
	FAsceBuildingPlacement Placement;
};
