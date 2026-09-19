// AsceDataTableSettings.cpp
#include "AsceDataTableSettings.h"

#include "DataTableSystem/AsceDataTableSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceDataTableSettings)

UAsceDataTableSettings::UAsceDataTableSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("Asce Data Tables");
}

#if WITH_EDITOR
void UAsceDataTableSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// 设置改了（比如新登记了一张 MDT）⇒ 立即重扫映射，编辑器里不用重启
	if (UAsceDataTableSubsystem* Sub = UAsceDataTableSubsystem::Get())
	{
		Sub->RefreshTablesFromSettings();
	}
}
#endif
