// AsceRowNameWrapper.h
#pragma once

#include "CoreMinimal.h"
#include "AsceRowNameWrapper.generated.h"

/**
 * 数据表行名包装：配置里引用"某张表的某一行"的统一类型。
 *
 * - **只存行名**（不存表指针）：表的唯一入口是 `UAsceDataTableSettings` 的别名（= 变量名）→ MDT；
 * - 编辑器下拉由 AsceEditor 模块的 `FAsceRowNameWrapperCustomization` 按属性 meta 渲染：
 *     `meta=(DataTableBinding="<别名>[:<字段1>,<字段2>...]")`
 *   别名 = Settings 里的 `TSoftObjectPtr<UDataTable>` 变量名；字段 = 行结构属性名（仅用于下拉里显示值）；
 * - 运行时查询：`UAsceDataTableSubsystem::GetDefinition<TRow>(Wrapper.RowName, Out)`。
 */
USTRUCT(BlueprintType)
struct ASCEGAME_API FAsceRowNameWrapper
{
	GENERATED_BODY()

	/** 行名（查 MDT 的 key；None = 未设置）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DataTable")
	FName RowName;

	FAsceRowNameWrapper() = default;
	FAsceRowNameWrapper(FName InRowName) : RowName(InRowName) {}

	bool IsNone() const { return RowName.IsNone(); }
	FName GetRowName() const { return RowName; }

	/** 方便直接当 FName 用（查表/日志）。 */
	operator FName() const { return RowName; }
};
