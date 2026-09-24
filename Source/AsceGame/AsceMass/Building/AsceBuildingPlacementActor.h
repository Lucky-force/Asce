// AsceBuildingPlacementActor.h
#pragma once

#include "GameFramework/Actor.h"
#include "AsceMass/Building/AsceBuildingTypes.h"
#include "DataTableSystem/AsceRowNameWrapper.h"
#include "AsceBuildingPlacementActor.generated.h"

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

	/** 建筑类型（`AsceEntityDefRow` 行名 = 数字 ID；下拉选行）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building",
		meta = (DataTableBinding = "AsceEntityDefRow:DisplayName,Description"))
	FAsceRowNameWrapper BuildingDef;

	/** 归属阵营（写进 `FAsceTeamFragment::TeamKey`）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	int32 TeamId = 0;

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

#if WITH_EDITORONLY_DATA
private:
	/** 编辑器吸附用的"虚拟自由位置"（逻辑格坐标，cm；未吸附）：
	 *  `OnConstruction` 在一次拖动**结束时**才跑，此时 Actor 位置 = 上次吸附位置 + 本次拖动增量。
	 *  若直接按当前位置吸回格心，本次增量就被吃掉（慢慢拖永远不动，必须一次甩过一格）。
	 *  所以先把增量累积到这里（跨多次拖动累积），再按累积位置吸附。 */
	FVector2D EditorFreeLocation = FVector2D::ZeroVector;
	FVector2D EditorLastSnappedLocation = FVector2D::ZeroVector;
	bool bEditorFreeLocationInitialized = false;

	/** 朝向同理：Yaw 增量累积到这里再吸附到 90° 倍数（否则每次不到半格就弹回）。 */
	float EditorFreeYaw = 0.f;
	float EditorLastSnappedYaw = 0.f;
	bool bEditorFreeYawInitialized = false;
#endif
};
