// AsceBuildingFragments.h
#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "AsceBuildingFragments.generated.h"

/**
 * **建造中**标记 + 建造信息。
 *
 * 建造流程（见 `建筑系统设计.md`）：
 *  1. 放置成功 ⇒ 创建实体，实体只有本 fragment（**没有** footprint ⇒ costfield/视野/Ether 的观察者都不会触发）；
 *  2. 完工（`BuildTimeSeconds` 到期，0 = 立即）⇒ 建筑系统给实体补 `FAsceGridFootprintFragment`（+ Ether 建筑标记）、
 *     删掉本 fragment ⇒ 观察者此时才真正生效；
 *  3. footprint 的 observer 跑过之后（`UAsceBuildingPendingObserverProcessor`）才释放"建造中占位"
 *     （`UAsceBuildingSubsystem` 的 pending 列表）—— 中间没有任何"两者都不挡"的空窗。
 *
 * 建造期**不挡路**（不写 costfield），只参与放置校验（不允许和建造中的工地重叠）。
 */
USTRUCT()
struct FAsceConstructingFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 建筑类型 id（完工时用不到，留给 UI/存档/日志）。 */
	FName BuildingDefId;

	/** footprint **未旋转**最小角 / 尺寸 / 朝向（完工时按它换算最终 AABB）。 */
	FIntPoint OriginGridLocation = FIntPoint::ZeroValue;
	FIntPoint Size = FIntPoint(1, 1);
	uint8 Rotation = 0;

	/** 完工时要写进 footprint 的遮挡高度。 */
	int32 OcclusionHeightCm = 0;

	/** 是不是 Ether 建筑（完工时决定要不要补 `FEtherBuildingFragment`）。 */
	bool bEtherBuilding = false;

	/** 开工时间（世界秒）与工期（0 = 立即完工）。 */
	double StartTimeSeconds = 0.0;
	float BuildTimeSeconds = 0.f;
};
