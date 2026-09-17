// Fill out your copyright notice in the Description page of Project Settings.

#include "GameFeatureAction_AddDataTableSources.h"

#include "Engine/CompositeDataTable.h"
#include "Engine/DataTable.h"
#include "DataTableSystem/AsceDataTableSubsystem.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameFeatureAction_AddDataTableSources)

#define LOCTEXT_NAMESPACE "GameFeatures"

void UGameFeatureAction_AddDataTableSources::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
	Super::OnGameFeatureActivating(Context);

	if (Sources.Num() == 0)
	{
		return;
	}

	UAsceDataTableSubsystem* Sub = UAsceDataTableSubsystem::Get();
	if (!Sub)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DataTable] %s：激活时没有数据表子系统（GameInstance 不存在？）"), *GetName());
		return;
	}

	TArray<FMountedEntry>& Mounted = MountedByContext.FindOrAdd(Context);
	for (const FAsceDataTableSourceEntry& Entry : Sources)
	{
		UDataTable* Master = Entry.MasterTable.LoadSynchronous();
		UDataTable* Source = Entry.SourceTable.LoadSynchronous();
		if (!Master || !Source)
		{
			UE_LOG(LogTemp, Warning, TEXT("[DataTable] %s：条目缺表（MDT=%s，SDT=%s）"),
				*GetName(), *Entry.MasterTable.ToString(), *Entry.SourceTable.ToString());
			continue;
		}
		if (Sub->GetMasterTable(Master->GetRowStruct()) != Master)
		{
			UE_LOG(LogTemp, Warning, TEXT("[DataTable] %s：MDT %s 没有在 UAsceDataTableSettings 里登记，挂了也查不到"),
				*GetName(), *Master->GetName());
		}
		if (Sub->AddSourceTable(Master, Source))
		{
			FMountedEntry E;
			E.Master = Master;
			E.Source = Source;
			Mounted.Add(E);
		}
	}
}

void UGameFeatureAction_AddDataTableSources::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	Super::OnGameFeatureDeactivating(Context);

	TArray<FMountedEntry> Mounted;
	MountedByContext.RemoveAndCopyValue(Context, Mounted);

	UAsceDataTableSubsystem* Sub = UAsceDataTableSubsystem::Get();
	for (const FMountedEntry& E : Mounted)
	{
		if (Sub && E.Master.IsValid() && E.Source.IsValid())
		{
			Sub->RemoveSourceTable(E.Master.Get(), E.Source.Get());
		}
	}
}

#if WITH_EDITOR
EDataValidationResult UGameFeatureAction_AddDataTableSources::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	for (int32 i = 0; i < Sources.Num(); ++i)
	{
		const FAsceDataTableSourceEntry& Entry = Sources[i];
		const UDataTable* Master = Entry.MasterTable.LoadSynchronous();
		const UDataTable* Source = Entry.SourceTable.LoadSynchronous();

		if (!Master)
		{
			Context.AddError(FText::Format(LOCTEXT("MissingMaster", "Sources[{0}]：MDT 未设置"), i));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		if (!Master->IsA<UCompositeDataTable>())
		{
			Context.AddError(FText::Format(LOCTEXT("NotComposite", "Sources[{0}]：{1} 不是 UCompositeDataTable（MDT）"),
				i, FText::FromString(Master->GetName())));
			Result = EDataValidationResult::Invalid;
		}
		if (!Source)
		{
			Context.AddError(FText::Format(LOCTEXT("MissingSource", "Sources[{0}]：SDT 未设置"), i));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		if (Master->GetRowStruct() != Source->GetRowStruct())
		{
			Context.AddError(FText::Format(LOCTEXT("RowStructMismatch", "Sources[{0}]：MDT({1}) 与 SDT({2}) 的行结构不一致"),
				i,
				FText::FromString(Master->GetRowStruct() ? Master->GetRowStruct()->GetName() : TEXT("None")),
				FText::FromString(Source->GetRowStruct() ? Source->GetRowStruct()->GetName() : TEXT("None"))));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
