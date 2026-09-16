// AsceDataTableSettings.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/DeveloperSettings.h"
#include "AsceDataTableSettings.generated.h"

/**
 * 项目数据表设置（Project Settings → Game → Asce Data Tables）。
 *
 * **别名 = 这里的变量名**：每个逻辑表一个 `TSoftObjectPtr<UDataTable>` 变量，指向该表的 **MDT（`UCompositeDataTable`）**；
 * 表的真实内容由 **SDT** 组成，运行时由 `UGameFeatureAction_AddDataTableSources` 把 SDT 挂到这张 MDT 上（合并查询）。
 *
 * 新增一张表：
 *   1. 这里加一行变量（**名字 = 表结构名去掉开头的 `F`**，编辑器里按名字就能认出是哪张表）；
 *   2. 表结构头文件里 `DECLARE_DATA_TABLE(别名)`；
 *   3. 某个 cpp 里 `IMPLEMENT_DATA_TABLE(别名)`。
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Asce Data Tables"))
class UAsceDataTableSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAsceDataTableSettings();

	// ── Ether ──

	/** Ether：端口类型表（MDT）。行结构：`FAsceEtherPortTypeRow`。 */
	UPROPERTY(EditAnywhere, config, Category = "Ether")
	TSoftObjectPtr<UDataTable> AsceEtherPortTypeRow;
};
