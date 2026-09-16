// AsceEtherPortTypeTable.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsceMass/Ether/AsceEtherTypes.h"
#include "AsceEtherPortTypeTable.generated.h"

/**
 * Ether **端口类型表**的一行（**行名 = 端口类型 id**）。
 *
 * 表体（MDT）在 `UAsceDataTableSettings::AsceEtherPortTypeRow` 里指定（变量名给人看，运行时按行结构类型解析），
 * 内容由各 GameFeature 的 SDT 组成；查询：
 * ```cpp
 * FAsceEtherPortTypeRow Row;
 * if (UAsceDataTableSubsystem::GetDefinition<FAsceEtherPortTypeRow>(TEXT("BulkPort"), Row)) { ... }
 * ```
 */
USTRUCT(BlueprintType)
struct FAsceEtherPortTypeRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 组合 op（Wire / Not / And / Or）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	EAsceEtherPortOp Op = EAsceEtherPortOp::Wire;

	/** 轴 1：每 tick 吞吐 = 该端口缓冲上限。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	int32 ThroughputPerTick = 1;

	/** 轴 2：发射距离（连线射线最大格数；只看发射端；`0 = 不能连线`）。
	 *  uint8：255 格 = 16 个扇区，足够远。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	uint8 LinkRangeCells = 255;
};
