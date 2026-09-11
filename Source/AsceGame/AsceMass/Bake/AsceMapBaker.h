// AsceMapBaker.h
#pragma once

#if WITH_EDITORONLY_DATA
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsceMapBaker.generated.h"

class UAsceBakedMapData;

/**
 * 编辑器烘焙器（摆在关卡里，配置后点 Bake）：
 *  - 高度：按视野格中心从 TraceMaxZCm 向下打 Visibility 射线取地形高度（绝对 Z，cm，0..65535），
 *    排除阻挡 Actor（BlockerClass）。
 *  - 阻挡：按流场格 XY AABB 与所有阻挡 Actor 的 XY 包围盒相交判定。
 * 结果写入 OutputData（UAsceBakedMapData）。
 */
UCLASS()
class AAsceMapBaker : public AActor
{
	GENERATED_BODY()

public:
	AAsceMapBaker();

	UPROPERTY(EditAnywhere, Category = "Asce|Bake")
	int32 SectorCountX = 4;

	UPROPERTY(EditAnywhere, Category = "Asce|Bake")
	int32 SectorCountY = 4;

	UPROPERTY(EditAnywhere, Category = "Asce|Bake", meta = (ClampMin = "1", ClampMax = "16"))
	int32 VisionDetailLevel = 1;

	UPROPERTY(EditAnywhere, Category = "Asce|Bake")
	FVector2D WorldOrigin = FVector2D::ZeroVector;

	/** 射线未命中时的默认高度（cm）。 */
	UPROPERTY(EditAnywhere, Category = "Asce|Bake")
	uint16 DefaultHeightCm = 0;

	/** 射线起始 Z（cm），向下打到 0；超出 [0,65535] 的命中忽略。 */
	UPROPERTY(EditAnywhere, Category = "Asce|Bake")
	float TraceMaxZCm = 65535.f;

	/** 专用阻挡 Actor 类：高度射线会排除它们；流场格与其 XY 包围盒相交则视为阻挡。 */
	UPROPERTY(EditAnywhere, Category = "Asce|Bake")
	TSubclassOf<AActor> BlockerClass;

	/** 输出数据资产（需先在 Content 里建好并指定）。 */
	UPROPERTY(EditAnywhere, Category = "Asce|Bake")
	TObjectPtr<UAsceBakedMapData> OutputData;

	UFUNCTION(CallInEditor, Category = "Asce|Bake")
	void Bake();
};
#endif // WITH_EDITORONLY_DATA
