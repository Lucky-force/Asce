// AsceBuildingDef.h
#pragma once

#include "CoreMinimal.h"
#include "AsceMass/Data/AsceEntityDefTypes.h"
#include "AsceBuildingDef.generated.h"

class UAsceBuildingCore;

/**
 * **建筑通用**类型定义（所有建筑种类的 Def 基类；实体定义表行的 `Def` 填它的子类）。
 *
 * - 占地（**未旋转**）/ 视野遮挡高度 / 行为 core —— 只要是建筑就有的字段；
 * - 具体建筑种类（Ether 建筑、以后的兵营等）派生并添加自己的字段；
 * - 运行时 footprint（`FAsceGridFootprintFragment`）由建筑系统按 placement 的
 *   `GetOccupiedBounds()` 换算成**旋转后的最终 AABB** 写入，不直接由本结构决定。
 */
USTRUCT(BlueprintType)
struct FAsceBuildingDefBase : public FAsceEntityDefBase
{
	GENERATED_BODY()

	/** 占地（**未旋转**；落库时按旋转换算成最终 AABB）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	int32 SizeX = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	int32 SizeY = 1;

	/** 视野遮挡高度（cm；0 = 不遮挡）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	int32 OcclusionHeightCm = 0;

	/** 建造工期（秒；0 = 立即完工。建造期间实体只有 `FAsceConstructingFragment`，不写 footprint）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	float BuildTimeSeconds = 0.f;

	/** 建筑行为 core（UI/功能入口；由 `UAsceBuildingSubsystem` 创建并持有）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	TSoftClassPtr<UAsceBuildingCore> CoreClass;
};
