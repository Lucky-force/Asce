// AsceEntityFactory.h
#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "AsceEntityFactory.generated.h"

/**
 * 实体生成**统一入口**（所有生成点都走这里；脚本/蓝图都能调）：
 * - 按实体定义表行解析生成模板（`Def.EntityTemplate`，`UMassEntityConfigAsset`）→ 生成实体；
 * - 把类型数据（目前 = `FAsceRTSUnitDef`）写进对应 fragment；
 * - 建筑不走这里（由 `UAsceBuildingSubsystem` 创建并做校验/登记）。
 */
UCLASS()
class UAsceEntityFactory : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 按定义生成实体。
	 * @param DefRowName        实体定义表行名（数字 ID 字符串）
	 * @param Transforms        每个实体的初始 Transform
	 * @param TeamKey           归属阵营（**由生成方决定**：玩家/中立等，写进 `FAsceTeamFragment`）
	 * @return 生成的实体 handle 数字（`FMassEntityHandle::AsNumber`；失败为空）
	 */
	UFUNCTION(BlueprintCallable, Category = "Asce|Entity")
	static TArray<int64> SpawnEntitiesByDef(UObject* WorldContextObject, FName DefRowName, const TArray<FTransform>& Transforms, int32 TeamKey = 0);
};
