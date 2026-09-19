// AsceInspectorTypes.h
#pragma once

#include "CoreMinimal.h"
#include "AsceInspectorTypes.generated.h"

/** 属性栏一行（控制器产出，widget 直接渲染）。 */
USTRUCT(BlueprintType)
struct FAsceInspectorStatRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Inspector")
	FText Label;

	UPROPERTY(BlueprintReadOnly, Category = "Inspector")
	FText Value;
};

/** 选择列表的一项（多选时左栏列表）。 */
USTRUCT(BlueprintType)
struct FAsceInspectorListEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Inspector")
	int64 EntityNumber = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Inspector")
	int32 TypeId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Inspector")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Inspector")
	TSoftObjectPtr<UTexture2D> HeadIcon;

	UPROPERTY(BlueprintReadOnly, Category = "Inspector")
	bool bIsPrimary = false;
};

/** GameplayMessage：选择集合变化（频道 `Asce.UI.SelectionChanged`）。 */
USTRUCT(BlueprintType)
struct FAsceSelectionChangedMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Inspector")
	int32 Count = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Inspector")
	int64 PrimaryEntityNumber = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Inspector")
	int32 PrimaryTypeId = 0;
};
