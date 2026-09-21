// AsceDataTableSubsystem.cpp
#include "AsceDataTableSubsystem.h"

#include "Engine/CompositeDataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "DataTableSystem/AsceDataTableSettings.h"
#include "UObject/UnrealType.h"
#include "UObject/Stack.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceDataTableSubsystem)

void UAsceDataTableSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RefreshTablesFromSettings();
}

void UAsceDataTableSubsystem::RefreshTablesFromSettings()
{
	const UAsceDataTableSettings* Settings = GetDefault<UAsceDataTableSettings>();
	if (!Settings)
	{
		return;
	}

	// **以 settings 为准重建**映射：先清再建。
	// 这两个映射的唯一来源就是 settings 里登记的 MDT（SDT 是挂到 MDT 的父表上，不在这里登记）
	// ⇒ 清空是安全的；否则改了配置/把别名清空后，旧映射会残留（PIE 里表现为"改了还是旧表"，重启才生效）。
	AliasToMaster.Reset();
	RowStructToMaster.Reset();

	// Settings 上每个 `TSoftObjectPtr<UDataTable>` 变量都是"一张逻辑表（MDT）"；变量名 = 别名
	for (TFieldIterator<FSoftObjectProperty> It(UAsceDataTableSettings::StaticClass()); It; ++It)
	{
		const FName Alias = It->GetFName();
		const FSoftObjectPtr Soft = It->GetPropertyValue_InContainer(Settings);
		if (Soft.IsNull())
		{
			continue;
		}
		UDataTable* Table = Cast<UDataTable>(Soft.Get());
		if (!Table)
		{
			Table = Cast<UDataTable>(Soft.LoadSynchronous());
		}
		if (Table)
		{
			RegisterTable(Table);
			AliasToMaster.Add(Alias, Table);
		}
	}
}

void UAsceDataTableSubsystem::RegisterTable(UDataTable* MasterTable)
{
	if (MasterTable)
	{
		RegisterTable(MasterTable->GetRowStruct(), MasterTable);
	}
}

void UAsceDataTableSubsystem::RegisterTable(const UScriptStruct* RowStruct, UDataTable* MasterTable)
{
	if (!RowStruct || !MasterTable)
	{
		return;
	}
	if (MasterTable->GetRowStruct() != RowStruct)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DataTable] 登记失败：表 %s 的行结构不是 %s"),
			*MasterTable->GetName(), *RowStruct->GetName());
		return;
	}
	RowStructToMaster.Add(TWeakObjectPtr<const UScriptStruct>(RowStruct), MasterTable);
}

void UAsceDataTableSubsystem::UnregisterTable(const UScriptStruct* RowStruct)
{
	if (RowStruct)
	{
		RowStructToMaster.Remove(TWeakObjectPtr<const UScriptStruct>(RowStruct));
	}
}

UDataTable* UAsceDataTableSubsystem::GetMasterTable(const UScriptStruct* RowStruct) const
{
	if (!RowStruct)
	{
		return nullptr;
	}
	const TWeakObjectPtr<UDataTable>* Found = RowStructToMaster.Find(TWeakObjectPtr<const UScriptStruct>(RowStruct));
	if (!Found)
	{
		return nullptr; // 这张表没登记（Settings 里没配 MDT）
	}
	UDataTable* Table = Found->Get();
	if (!Table)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DataTable] 已登记的表已被 GC：RowStruct=%s"), *RowStruct->GetName());
	}
	return Table;
}

UDataTable* UAsceDataTableSubsystem::GetMasterTableByAlias(FName Alias) const
{
	if (Alias.IsNone())
	{
		return nullptr;
	}
	const TWeakObjectPtr<UDataTable>* Found = AliasToMaster.Find(Alias);
	return Found ? Found->Get() : nullptr;
}

bool UAsceDataTableSubsystem::GetRowNames(const UScriptStruct* RowStruct, TArray<FName>& OutRowNames) const
{
	const UDataTable* Table = GetMasterTable(RowStruct);
	if (!Table)
	{
		return false;
	}
	Table->GetRowMap().GenerateKeyArray(OutRowNames);
	OutRowNames.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	return true;
}

UDataTable* UAsceDataTableSubsystem::BP_GetMasterTable(const UScriptStruct* RowStruct)
{
	UAsceDataTableSubsystem* Sub = UAsceDataTableSubsystem::Get();
	return Sub ? Sub->GetMasterTable(RowStruct) : nullptr;
}

bool UAsceDataTableSubsystem::Generic_GetDefinitionByStruct(const UScriptStruct* RowStruct, FName RowName, void* OutRowPtr)
{
	if (!RowStruct || !OutRowPtr || RowName.IsNone())
	{
		return false;
	}
	UAsceDataTableSubsystem* Sub = UAsceDataTableSubsystem::Get();
	UDataTable* Table = Sub ? Sub->GetMasterTable(RowStruct) : nullptr;
	if (!Table)
	{
		return false;
	}
	void* RowPtr = Table->FindRowUnchecked(RowName);
	if (!RowPtr)
	{
		return false;
	}
	RowStruct->CopyScriptStruct(OutRowPtr, RowPtr);   // 反射拷贝（不用 memcpy，安全）
	return true;
}

bool UAsceDataTableSubsystem::GetDefinitionByStruct(const UScriptStruct* RowStruct, FName RowName, FTableRowBase& OutRow)
{
	// CustomThunk：正常不会走到这里（蓝图走 execGetDefinitionByStruct）
	check(0);
	return false;
}

DEFINE_FUNCTION(UAsceDataTableSubsystem::execGetDefinitionByStruct)
{
	P_GET_OBJECT(UScriptStruct, RowStruct);
	P_GET_PROPERTY(FNameProperty, RowName);

	Stack.StepCompiledIn<FStructProperty>(NULL);
	void* OutRowPtr = Stack.MostRecentPropertyAddress;

	P_FINISH;

	bool bSuccess = false;
	const FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);
	// 输出结构类型必须与传入的行结构类型一致
	if (StructProp && OutRowPtr && RowStruct && StructProp->Struct.Get() == RowStruct)
	{
		P_NATIVE_BEGIN;
		bSuccess = UAsceDataTableSubsystem::Generic_GetDefinitionByStruct(RowStruct, RowName, OutRowPtr);
		P_NATIVE_END;
	}
	*(bool*)RESULT_PARAM = bSuccess;
}

UAsceDataTableSubsystem* UAsceDataTableSubsystem::Get()
{
	// 引擎子系统：编辑器（无 GameInstance）与运行时都存在 ⇒ 编辑器工具/GameFeature 编辑器激活也能查表
	return GEngine ? GEngine->GetEngineSubsystem<UAsceDataTableSubsystem>() : nullptr;
}

bool UAsceDataTableSubsystem::CheckRowStruct(const UDataTable* Table, const UScriptStruct* RowStruct) const
{
	if (!Table || !RowStruct)
	{
		return false;
	}
	if (Table->GetRowStruct() != RowStruct)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DataTable] 行结构不匹配：表 %s（RowStruct=%s）期望 %s"),
			*Table->GetName(),
			Table->GetRowStruct() ? *Table->GetRowStruct()->GetName() : TEXT("None"),
			*RowStruct->GetName());
		return false;
	}
	return true;
}

bool UAsceDataTableSubsystem::AddSourceTable(UDataTable* MasterTable, UDataTable* SourceTable)
{
	if (!MasterTable || !SourceTable || MasterTable == SourceTable)
	{
		return false;
	}
	if (MasterTable->GetRowStruct() != SourceTable->GetRowStruct())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DataTable] SDT 行结构与 MDT 不一致，拒绝挂载：%s -> %s"),
			*SourceTable->GetName(), *MasterTable->GetName());
		return false;
	}
	UCompositeDataTable* Composite = Cast<UCompositeDataTable>(MasterTable);
	if (!Composite)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DataTable] 目标表不是 UCompositeDataTable（MDT）：%s"), *MasterTable->GetName());
		return false;
	}

	// 防重：编辑器激活挂过之后资产若被保存，`ParentTables` 里可能已经有它 ⇒ 先摘再挂，保证只挂一份
	Composite->RemoveParentTable(SourceTable);
	Composite->AddParentTable(SourceTable);   // 运行时挂载（内建"后添加覆盖"）

	TArray<TWeakObjectPtr<UDataTable>>& Sources = MountedSources.FindOrAdd(MasterTable);
	Sources.AddUnique(SourceTable);
	return true;
}

bool UAsceDataTableSubsystem::RemoveSourceTable(UDataTable* MasterTable, UDataTable* SourceTable)
{
	if (!MasterTable || !SourceTable)
	{
		return false;
	}
	UCompositeDataTable* Composite = Cast<UCompositeDataTable>(MasterTable);
	if (!Composite)
	{
		return false;
	}
	Composite->RemoveParentTable(SourceTable);

	if (TArray<TWeakObjectPtr<UDataTable>>* Sources = MountedSources.Find(MasterTable))
	{
		Sources->Remove(SourceTable);
		if (Sources->Num() == 0)
		{
			MountedSources.Remove(MasterTable);
		}
	}
	return true;
}

void UAsceDataTableSubsystem::DumpTables() const
{
	UE_LOG(LogTemp, Log, TEXT("[DataTable] 已登记 %d 张 MDT、%d 张有关联来源"), RowStructToMaster.Num(), MountedSources.Num());
	for (const TPair<TWeakObjectPtr<const UScriptStruct>, TWeakObjectPtr<UDataTable>>& Pair : RowStructToMaster)
	{
		const UScriptStruct* RowStruct = Pair.Key.Get();
		const UDataTable* Table = Pair.Value.Get();
		TArray<TWeakObjectPtr<UDataTable>> Sources;
		if (const TArray<TWeakObjectPtr<UDataTable>>* Found = MountedSources.Find(Pair.Value))
		{
			Sources = *Found;
		}
		UE_LOG(LogTemp, Log, TEXT("  RowStruct=%s  MDT=%s  来源 %d 张"),
			RowStruct ? *RowStruct->GetName() : TEXT("<gone>"),
			Table ? *Table->GetName() : TEXT("<gone>"),
			Sources.Num());
		for (const TWeakObjectPtr<UDataTable>& Src : Sources)
		{
			const UDataTable* S = Src.Get();
			UE_LOG(LogTemp, Log, TEXT("    SDT %s"), S ? *S->GetName() : TEXT("<gone>"));
		}
	}
}

static FAutoConsoleCommand GRefreshDataTablesCmd(
	TEXT("Asce.DataTables.Refresh"),
	TEXT("以 UAsceDataTableSettings 为准重建 别名/行结构→MDT 映射（先清后建）并 dump"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (UAsceDataTableSubsystem* Sub = UAsceDataTableSubsystem::Get())
		{
			Sub->RefreshTablesFromSettings();
			Sub->DumpTables();
		}
	}));
