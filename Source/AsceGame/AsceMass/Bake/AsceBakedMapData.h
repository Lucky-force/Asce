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

	UPROPERTY(EditAnywhere, Category = "Asce|Baked")
	FVector2D WorldOrigin = FVector2D::ZeroVector;

	// —— 数据 ——
	/** 视野格地形绝对高度（cm，0..65535），长度 = VisionGridW * VisionGridH。 */
	UPROPERTY(EditAnywhere, Category = "Asce|Baked")
	TArray<uint16> StaticHeights;

	/** 流场格阻挡（1=阻挡），长度 = FlowGridW * FlowGridH。 */
	UPROPERTY(EditAnywhere, Category = "Asce|Baked")
	TArray<uint8> Blocked;
};
