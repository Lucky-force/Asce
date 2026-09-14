// AsceBakedMapData.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AsceBakedMapData.generated.h"

/**
 * 编辑器烘焙出来的初始地图数据（独立的 DataAsset，两端加载同一份 → 确定性一致）。
 * - StaticHeights：按**视野格**存地形绝对高度 Z（cm，0..65535），喂给 FAsceVisionHeightMap。
 * - Blocked：按**流场格**存阻挡（1=阻挡），喂给 FWorldMapData 的 cost。
 * 尺寸字段冗余存一份，用于运行时校验（防 actor 配置与烘焙配置不一致）。
 */
UCLASS(BlueprintType)
class UAsceBakedMapData : public UDataAsset
{
	GENERATED_BODY()

public:
	// —— 尺寸（校验用） ——
	UPROPERTY(EditAnywhere, Category = "Asce|Baked")
	int32 SectorCountX = 0;

	UPROPERTY(EditAnywhere, Category = "Asce|Baked")
	int32 SectorCountY = 0;

	UPROPERTY(EditAnywhere, Category = "Asce|Baked")
	float VisionCellSizeCm = 50.f;

	UPROPERTY(EditAnywhere, Category = "Asce|Baked")
	int32 FlowGridW = 0;

	UPROPERTY(EditAnywhere, Category = "Asce|Baked")
	int32 FlowGridH = 0;

	UPROPERTY(EditAnywhere, Category = "Asce|Baked")
	int32 VisionGridW = 0;

	UPROPERTY(EditAnywhere, Category = "Asce|Baked")
	int32 VisionGridH = 0;

	// —— 数据 ——
	// 注意：大数组刻意不用 EditAnywhere/VisibleAnywhere——几百万级的 TArray 会让 Details 面板
	// 为每个元素生成属性节点（打开资产时编辑器卡死）。这里只保留 UPROPERTY 参与序列化，
	// 数据内容由 AAsceMapBaker 的 Bake() 写入，运行时从 DA 读取。

	/** 视野格地形绝对高度（cm，0..65535），长度 = VisionGridW * VisionGridH。 */
	UPROPERTY()
	TArray<uint16> StaticHeights;

	/** 流场格阻挡布尔（0=可通行 / 1=阻挡），长度 = FlowGridW * FlowGridH。
	 *  只存布尔语义、与 CostField 内部编码解耦——运行时由 AAsceWorldMapReplicatorActor
	 *  转成 CostField 编码写入：1=可通行、0b10000000=墙。想省内存可后续 bit 化（1bit/格）。 */
	UPROPERTY()
	TArray<uint8> Blocked;
};
