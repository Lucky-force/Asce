// AsceUnitDef.h
#pragma once

#include "CoreMinimal.h"
#include "AsceMass/Data/AsceEntityDefTypes.h"
#include "AsceUnitDef.generated.h"

/**
 * RTS 单位类型定义（实体定义表行 `Def` 的一种）：只放"同一结构、数值不同"的每类型数据。
 *
 * - **结构**（fragment/tag 组合、能力开关）由 trait 资产决定；
 * - 本结构的值由 `UAsceEntityFactory::SpawnEntitiesByDef` 在生成时写进对应 fragment；
 * - 以后模型/攻击模式等表现数据继续往这里加。
 */
USTRUCT(BlueprintType)
struct FAsceRTSUnitDef : public FAsceEntityDefBase
{
	GENERATED_BODY()

	// ── 移动 ──

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	float MaxSpeed = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	float MaxAcceleration = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	float DefaultDesiredSpeed = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	float Radius = 75.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	float Height = 150.f;

	// ── 视野 ──

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vision")
	int32 SightRadius = 20;

	// ── 属性 ──

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
	float MaxHealth = 100.f;
};
