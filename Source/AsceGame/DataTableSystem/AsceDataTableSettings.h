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

#if WITH_EDITOR
	/** 改设置（如新加/改指向的 MDT）后立即重扫，不用重启编辑器。 */
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ── Ether ──

	/** Ether：端口类型表（MDT）。行结构：`FAsceEtherPortTypeRow`。 */
	UPROPERTY(EditAnywhere, config, Category = "Ether")
	TSoftObjectPtr<UDataTable> AsceEtherPortTypeRow;

	// ── 实体 ──

	/** 实体类型定义表（MDT）。行结构：`FAsceEntityDefRow`（行名 = 数字 ID 字符串）。 */
	UPROPERTY(EditAnywhere, config, Category = "Entity")
	TSoftObjectPtr<UDataTable> AsceEntityDefRow;

	// ── 战斗 ──

	/** 动作表（MDT）。行结构：`FAsceActionDef`。
	 *  用途：`FAsceRowNameWrapper` 的 `DataTableBinding = "AsceActionDefRow:..."` 下拉（攻击动作/技能槽位引用）。
	 *  ★ 查表本身走 `FAsceActionDefLibrary`（按行结构找表），**不依赖**这里是否填；这里只影响编辑器下拉。 */
	UPROPERTY(EditAnywhere, config, Category = "Combat")
	TSoftObjectPtr<UDataTable> AsceActionDefRow;
};
