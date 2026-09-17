// AsceBuildingPlacementActor.h
#pragma once

#include "GameFramework/Actor.h"
#include "AsceMass/Building/AsceBuildingTypes.h"
#include "AsceBuildingPlacementActor.generated.h"

class UAsceEtherBuildingConfig;
class UStaticMeshComponent;

/**
 * 地图作者摆放的建筑（**引导用**，见 `AsceMass/Building/建筑系统设计.md`）：
 * - 位置 = footprint **中心**（自动吸附到逻辑格中心）；Yaw 吸附到 90° 的倍数（`Rotation` = 顺时针次数）；
 * - `BeginPlay` 向 `UAsceBuildingSubsystem` 报到；子系统在一切就绪后创建真实建筑实体，然后**销毁本 Actor**（两端）；
 * - 编辑器里用一个方块标记显示实际占格（游戏中隐藏）。
 */
UCLASS()
class AAsceBuildingPlacementActor : public AActor
{
	GENERATED_BODY()

public:
	AAsceBuildingPlacementActor();

	/** 建筑类型（决定占格尺寸 / 遮挡高度 / Ether 配置）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	TSubclassOf<UAsceEtherBuildingConfig> BuildingConfig;

	/** 登记用建筑 id（None ⇒ 取配置类名）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	FName BuildingDefId;

	/** 归属阵营（写进 `FAsceTeamFragment::TeamKey`）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	int32 TeamKey = 0;

	/** 关掉就不会创建（地图作者临时禁用）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	bool bEnabled = true;

	/** 由 Actor 位置/旋转换算出摆放数据。 */
	FAsceBuildingPlacement BuildPlacement() const;

	/** 编辑器占格标记（游戏中隐藏）。 */
	UPROPERTY(VisibleAnywhere, Category = "Building")
	TObjectPtr<UStaticMeshComponent> FootprintVisual;

	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
#endif
};
