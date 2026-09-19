// AsceDataTableSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Subsystems/EngineSubsystem.h"
#include "AsceDataTableSubsystem.generated.h"

class UDataTable;

/**
 * 项目查表系统（**引擎子系统 = 进程级全局，编辑器和运行时都存在**；只查 MDT）。
 *
 * - **MDT**（`UCompositeDataTable`）= 一个逻辑表的唯一入口（本体为空，行来自各 **SDT**）；
 * - **SDT** 由 `UGameFeatureAction_AddDataTableSources` 在 GameFeature 激活/卸载时挂/卸（运行时合并，内建"后添加覆盖"）；
 * - **注册时按"行结构类型 → MDT"建映射**（扫 `UAsceDataTableSettings` 里登记的 MDT）⇒ 查表只传**行结构类型**，不用管表在哪；
 * - C++ 用模板：`FindRow<FAsceEtherPortTypeRow>(RowName, OutRow)`；
 *   蓝图用 `BP_GetMasterTable(RowStruct)` 拿表，再用引擎的 `Get Data Table Row` 节点读行；
 * - **配置里的外键**用 `FAsceRowNameWrapper`（只存行名 + 属性 meta 指定表别名，编辑器里出下拉）；
 * - **查询失败一律 `return false`、不动 out 引用**（调用方自己准备默认值）；行结构不匹配会告警。
 */
UCLASS()
class UAsceDataTableSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 取引擎子系统（编辑器/运行时都存在；静态模板查询内部用它）。 */
	ASCEGAME_API static UAsceDataTableSubsystem* Get();

	// ── 静态模板查询（**推荐入口**：只传行结构类型，内部自己取子系统） ──

	/** 查一行；没登记表 / 行不存在 / 行结构不符 ⇒ false（不动 OutRow）。 */
	template<typename TRow>
	static bool GetDefinition(FName RowName, TRow& OutRow)
	{
		UAsceDataTableSubsystem* Sub = Get();
		return Sub ? Sub->FindRow<TRow>(RowName, OutRow) : false;
	}

	/** 取全部行（**追加**到 OutRows）。 */
	template<typename TRow>
	static bool GetDefinitions(TArray<TRow>& OutRows)
	{
		UAsceDataTableSubsystem* Sub = Get();
		return Sub ? Sub->GetAllRows<TRow>(OutRows) : false;
	}

	/** 取行表（**追加**到 OutRowMap）。 */
	template<typename TRow>
	static bool GetDefinitionMap(TMap<FName, TRow>& OutRowMap)
	{
		UAsceDataTableSubsystem* Sub = Get();
		return Sub ? Sub->GetRowMap<TRow>(OutRowMap) : false;
	}

	// ── 注册（行结构 → MDT） ──

	/** 登记一张 MDT：`RowStruct = MasterTable->GetRowStruct()`。子系统初始化时按 Settings 自动登记，也可手动加。 */
	void RegisterTable(UDataTable* MasterTable);
	void RegisterTable(const UScriptStruct* RowStruct, UDataTable* MasterTable);
	void UnregisterTable(const UScriptStruct* RowStruct);

	/** 按**行结构类型**取 MDT（表指针的唯一来源）。 */
	UDataTable* GetMasterTable(const UScriptStruct* RowStruct) const;

	/** 按**别名**（= `UAsceDataTableSettings` 变量名）取 MDT（编辑器下拉/工具用）；没登记/别名不存在 ⇒ nullptr。 */
	ASCEGAME_API UDataTable* GetMasterTableByAlias(FName Alias) const;

	/** 取某行结构的 MDT 全部行名（按名排序；**覆盖** OutRowNames）；没有表 ⇒ false。 */
	ASCEGAME_API bool GetRowNames(const UScriptStruct* RowStruct, TArray<FName>& OutRowNames) const;

	/** 蓝图入口：按行结构取 MDT（拿到后用引擎的 `Get Data Table Row` 节点读行）。 */
	UFUNCTION(BlueprintCallable, Category = "Asce|DataTable", meta = (DisplayName = "Get Master Data Table"))
	static UDataTable* BP_GetMasterTable(const UScriptStruct* RowStruct);

	/**
	 * 蓝图：按**行结构类型**查一行（泛型节点 —— 蓝图里 `RowStruct` 是 **Struct** 引脚，`OutRow` 的结构类型须与之一致）。
	 * 失败返回 false（不动 OutRow）。C++ 请用 `GetDefinition<TRow>`。
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Asce|DataTable",
		meta = (CustomStructureParam = "OutRow", DisplayName = "Get Data Table Row By Struct"))
	static bool GetDefinitionByStruct(const UScriptStruct* RowStruct, FName RowName, FTableRowBase& OutRow);

	static bool Generic_GetDefinitionByStruct(const UScriptStruct* RowStruct, FName RowName, void* OutRowPtr);
	DECLARE_FUNCTION(execGetDefinitionByStruct);

	// ── 表挂载（GameFeatureAction 调用） ──

	/** 把 `SourceTable` 挂到 `MasterTable`（MDT）。校验：有效、行结构一致、不是自己。 */
	bool AddSourceTable(UDataTable* MasterTable, UDataTable* SourceTable);

	/** 从 `MasterTable` 摘掉 `SourceTable`。 */
	bool RemoveSourceTable(UDataTable* MasterTable, UDataTable* SourceTable);

	/** 调试：dump 已登记的表与已挂载的来源。 */
	void DumpTables() const;

	/**
	 * **重扫 `UAsceDataTableSettings` 并登记/更新映射**（别名 → MDT、行结构 → MDT）。
	 * 触发点：子系统初始化、编辑器改设置（`PostEditChangeProperty`）、GameFeature 激活、
	 * 控制台 `Asce.DataTables.Refresh`。重复调用安全（覆盖式更新）。
	 */
	ASCEGAME_API void RefreshTablesFromSettings();

	// ── 类型化查询（**只传行结构类型**；宏与业务代码都用这套） ──

	/** 查一行；没登记表 / 行不存在 / 行结构不符 ⇒ false（不动 OutRow）。 */
	template<typename TRow>
	bool FindRow(FName RowName, TRow& OutRow) const;

	/** 取全部行（**追加**到 OutRows）。 */
	template<typename TRow>
	bool GetAllRows(TArray<TRow>& OutRows) const;

	/** 取行表（**追加**到 OutRowMap）。 */
	template<typename TRow>
	bool GetRowMap(TMap<FName, TRow>& OutRowMap) const;

	// ── 指定表的查询（工具/特殊场景） ──

	template<typename TRow>
	bool FindRow(const UDataTable* Table, FName RowName, TRow& OutRow) const;

	template<typename TRow>
	bool GetAllRows(const UDataTable* Table, TArray<TRow>& OutRows) const;

	template<typename TRow>
	bool GetRowMap(const UDataTable* Table, TMap<FName, TRow>& OutRowMap) const;

private:
	/** 行结构校验（不匹配时告警）。 */
	bool CheckRowStruct(const UDataTable* Table, const UScriptStruct* RowStruct) const;

	/** 行结构 -> MDT（弱引用，GC 安全；结构或表被销毁后自动失效）。 */
	TMap<TWeakObjectPtr<const UScriptStruct>, TWeakObjectPtr<UDataTable>> RowStructToMaster;

	/** 别名（= `UAsceDataTableSettings` 变量名）-> MDT（弱引用）；编辑器下拉/工具按别名解析用。 */
	TMap<FName, TWeakObjectPtr<UDataTable>> AliasToMaster;

	/** MDT -> 已挂上的来源表（统计 / 调试 / 整体卸载用）。 */
	TMap<TWeakObjectPtr<UDataTable>, TArray<TWeakObjectPtr<UDataTable>>> MountedSources;
};

// ─────────────────────────────────────────────────────────────
// 模板实现
// ─────────────────────────────────────────────────────────────

template<typename TRow>
bool UAsceDataTableSubsystem::FindRow(FName RowName, TRow& OutRow) const
{
	return FindRow(GetMasterTable(TRow::StaticStruct()), RowName, OutRow);
}

template<typename TRow>
bool UAsceDataTableSubsystem::GetAllRows(TArray<TRow>& OutRows) const
{
	return GetAllRows(GetMasterTable(TRow::StaticStruct()), OutRows);
}

template<typename TRow>
bool UAsceDataTableSubsystem::GetRowMap(TMap<FName, TRow>& OutRowMap) const
{
	return GetRowMap(GetMasterTable(TRow::StaticStruct()), OutRowMap);
}

template<typename TRow>
bool UAsceDataTableSubsystem::FindRow(const UDataTable* Table, FName RowName, TRow& OutRow) const
{
	static_assert(TIsDerivedFrom<TRow, FTableRowBase>::IsDerived, "TRow 必须是 FTableRowBase 派生的表行结构");
	if (!Table || RowName.IsNone() || !CheckRowStruct(Table, TRow::StaticStruct()))
	{
		return false;
	}
	// 不用 FindRow<T>（它内部有 check，结构不符会崩），自己按无校验路径取
	uint8* const* Found = Table->GetRowMap().Find(RowName);
	if (!Found || !*Found)
	{
		return false;
	}
	OutRow = *reinterpret_cast<const TRow*>(*Found);
	return true;
}

template<typename TRow>
bool UAsceDataTableSubsystem::GetAllRows(const UDataTable* Table, TArray<TRow>& OutRows) const
{
	static_assert(TIsDerivedFrom<TRow, FTableRowBase>::IsDerived, "TRow 必须是 FTableRowBase 派生的表行结构");
	if (!Table || !CheckRowStruct(Table, TRow::StaticStruct()))
	{
		return false;
	}
	const TMap<FName, uint8*>& RowMap = Table->GetRowMap();
	OutRows.Reserve(OutRows.Num() + RowMap.Num());
	for (const TPair<FName, uint8*>& Pair : RowMap)
	{
		if (Pair.Value)
		{
			OutRows.Add(*reinterpret_cast<const TRow*>(Pair.Value));
		}
	}
	return true;
}

template<typename TRow>
bool UAsceDataTableSubsystem::GetRowMap(const UDataTable* Table, TMap<FName, TRow>& OutRowMap) const
{
	static_assert(TIsDerivedFrom<TRow, FTableRowBase>::IsDerived, "TRow 必须是 FTableRowBase 派生的表行结构");
	if (!Table || !CheckRowStruct(Table, TRow::StaticStruct()))
	{
		return false;
	}
	const TMap<FName, uint8*>& RowMap = Table->GetRowMap();
	OutRowMap.Reserve(OutRowMap.Num() + RowMap.Num());
	for (const TPair<FName, uint8*>& Pair : RowMap)
	{
		if (Pair.Value)
		{
			OutRowMap.Add(Pair.Key, *reinterpret_cast<const TRow*>(Pair.Value));
		}
	}
	return true;
}
