// AsceEntityDefLibrary.cpp
#include "AsceEntityDefLibrary.h"

#include "DataTableSystem/AsceDataTableSubsystem.h"
#include "Engine/DataTable.h"
#include "HAL/IConsoleManager.h"

namespace
{
	UDataTable* GetEntityDefTable()
	{
		const UAsceDataTableSubsystem* Sub = UAsceDataTableSubsystem::Get();
		return Sub ? Sub->GetMasterTable(FAsceEntityDefRow::StaticStruct()) : nullptr;
	}

	bool TryExtractEtherDef(const FAsceEntityDefRow& Row, FAsceEtherBuildingDef& OutDef)
	{
		const FAsceEtherBuildingDef* DefPtr = Row.Def.GetPtr<FAsceEtherBuildingDef>();
		if (!DefPtr)
		{
			return false;
		}
		OutDef = *DefPtr;
		return true;
	}

	bool TryExtractBuildingDef(const FAsceEntityDefRow& Row, FAsceBuildingDefBase& OutDef)
	{
		const FAsceBuildingDefBase* DefPtr = Row.Def.GetPtr<FAsceBuildingDefBase>();
		if (!DefPtr)
		{
			return false;
		}
		OutDef = *DefPtr;
		return true;
	}
}

bool UAsceEntityDefLibrary::ParseEntityDefId(FName RowName, uint16& OutId)
{
	OutId = 0;
	if (RowName.IsNone())
	{
		return false;
	}

	const FString Str = RowName.ToString();
	if (Str.IsEmpty() || Str.Len() > 5)
	{
		return false;
	}

	int32 Value = 0;
	for (const TCHAR Ch : Str)
	{
		if (!FChar::IsDigit(Ch))
		{
			return false;
		}
		Value = Value * 10 + static_cast<int32>(Ch - TEXT('0'));
		if (Value > MAX_uint16)
		{
			return false;
		}
	}
	OutId = static_cast<uint16>(Value);
	return true;
}

uint16 UAsceEntityDefLibrary::ParseEntityDefIdChecked(FName RowName)
{
	uint16 Id = 0;
	const bool bParsed = ParseEntityDefId(RowName, Id);
	// 0 保留给"未配置"（`FAsceUnitDefFragment::UniqueTypeId` 的默认值）⇒ 行名 "0" 也非法
	checkf(bParsed && Id != 0,
		TEXT("实体类型 id 非法：行名 '%s' 必须是 1..65535 的数字字符串（0 保留给'未配置'）"),
		*RowName.ToString());
	return Id;
}

bool UAsceEntityDefLibrary::GetEntityDefRow(int32 Id, FAsceEntityDefRow& OutRow)
{
	if (Id < 0 || Id > MAX_uint16)
	{
		return false;
	}
	const FName RowName(*FString::FromInt(Id));
	return UAsceDataTableSubsystem::GetDefinition<FAsceEntityDefRow>(RowName, OutRow);
}

bool UAsceEntityDefLibrary::GetEntityDefRowByName(FName RowName, FAsceEntityDefRow& OutRow)
{
	return UAsceDataTableSubsystem::GetDefinition<FAsceEntityDefRow>(RowName, OutRow);
}

bool UAsceEntityDefLibrary::GetEtherBuildingDef(int32 Id, FAsceEtherBuildingDef& OutDef)
{
	FAsceEntityDefRow Row;
	return GetEntityDefRow(Id, Row) && TryExtractEtherDef(Row, OutDef);
}

bool UAsceEntityDefLibrary::GetEtherBuildingDefByName(FName RowName, FAsceEtherBuildingDef& OutDef)
{
	FAsceEntityDefRow Row;
	return GetEntityDefRowByName(RowName, Row) && TryExtractEtherDef(Row, OutDef);
}

bool UAsceEntityDefLibrary::GetBuildingDef(int32 Id, FAsceBuildingDefBase& OutDef)
{
	FAsceEntityDefRow Row;
	return GetEntityDefRow(Id, Row) && TryExtractBuildingDef(Row, OutDef);
}

bool UAsceEntityDefLibrary::GetBuildingDefByName(FName RowName, FAsceBuildingDefBase& OutDef)
{
	FAsceEntityDefRow Row;
	return GetEntityDefRowByName(RowName, Row) && TryExtractBuildingDef(Row, OutDef);
}

int32 UAsceEntityDefLibrary::ValidateEntityDefTable(TArray<FString>& OutErrors)
{
	OutErrors.Reset();

	const UDataTable* Table = GetEntityDefTable();
	if (!Table)
	{
		OutErrors.Add(TEXT("实体定义表未登记/未加载（检查 UAsceDataTableSettings::AsceEntityDefRow 与 GameFeature 挂载）"));
		UE_LOG(LogTemp, Warning, TEXT("[EntityDef] %s"), *OutErrors.Last());
		return 1;
	}

	// 行名 → id：① 必须是 1..65535 的数字串（**0 保留**给"未配置"哨兵，禁止行名 "0"）；
	//           ② 别名冲突（"1" / "01" / "001" 解析成同一个 id）也要报，否则两个类型会互相覆盖
	TMap<uint16, FName> IdToRowName;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		uint16 Id = 0;
		if (!ParseEntityDefId(Pair.Key, Id))
		{
			OutErrors.Add(FString::Printf(TEXT("行名 '%s' 不是 1..65535 的数字字符串"), *Pair.Key.ToString()));
			continue;
		}
		if (Id == 0)
		{
			OutErrors.Add(FString::Printf(TEXT("行名 '%s' 非法：id 0 保留给'未配置'（`UniqueTypeId` 默认值）"), *Pair.Key.ToString()));
			continue;
		}
		if (const FName* Existing = IdToRowName.Find(Id))
		{
			OutErrors.Add(FString::Printf(TEXT("别名冲突：行名 '%s' 与 '%s' 都解析成 id %u"),
				*Pair.Key.ToString(), *Existing->ToString(), static_cast<uint32>(Id)));
			continue;
		}
		IdToRowName.Add(Id, Pair.Key);

		if (!Pair.Value)
		{
			OutErrors.Add(FString::Printf(TEXT("行 '%s' 数据为空"), *Pair.Key.ToString()));
			continue;
		}

		const FAsceEntityDefRow* Row = reinterpret_cast<const FAsceEntityDefRow*>(Pair.Value);
		if (!Row->Def.IsValid())
		{
			OutErrors.Add(FString::Printf(TEXT("行 '%s'：Def 未配置"), *Pair.Key.ToString()));
			continue;
		}
		if (!Row->Def.GetScriptStruct()->IsChildOf(FAsceEntityDefBase::StaticStruct()))
		{
			OutErrors.Add(FString::Printf(TEXT("行 '%s'：Def 类型 %s 不是 FAsceEntityDefBase 子类"),
				*Pair.Key.ToString(), *Row->Def.GetScriptStruct()->GetName()));
		}
	}

	for (const FString& Error : OutErrors)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EntityDef] %s"), *Error);
	}
	return OutErrors.Num();
}

static FAutoConsoleCommand GValidateEntityDefCmd(
	TEXT("Asce.ValidateEntityDef"),
	TEXT("校验实体定义表（行名必须是 1..65535 的数字串、0 保留、无别名冲突、Def 是 FAsceEntityDefBase 子类）"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		TArray<FString> Errors;
		const int32 Count = UAsceEntityDefLibrary::ValidateEntityDefTable(Errors);
		UE_LOG(LogTemp, Log, TEXT("[EntityDef] 校验完成：%d 个错误"), Count);
	}));
