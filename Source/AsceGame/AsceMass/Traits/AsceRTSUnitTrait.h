#pragma once

#include "MassEntityTraitBase.h"
#include "AsceRTSUnitTrait.generated.h"

/**
 * "单位"专有 Trait（会动 / 会打 / 有视野；**建筑不要挂这个**）。
 *
 * - 公共身份/阵营/血量/NetID 在 `UAsceRTSEntityTrait`（同一个 MassEntityConfig 里两个一起挂）；
 * - 本 Trait 只负责单位专有结构：移动 fragment + 每实体调参 / 视野 / 订单 / 攻击·交互能力开关；
 * - 下面的数值只是**默认值**，生成时由 `UAsceEntityFactory` 按实体定义表（`FAsceRTSUnitDef`）覆盖。
 *
 * 移动/攻击/交互能力通过开关决定是否挂对应 fragment（结构差异），避免每个单位塞满不需要的 fragment。
 */
UCLASS(BlueprintType, meta = (DisplayName = "Asce RTS Unit"))
class UAsceRTSUnitTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	// ── 移动默认值 ──
	UPROPERTY(EditAnywhere, Category = "Movement")
	float MaxSpeed = 300.f;

	UPROPERTY(EditAnywhere, Category = "Movement")
	float MaxAcceleration = 350.f;

	UPROPERTY(EditAnywhere, Category = "Movement")
	float DefaultDesiredSpeed = 200.f;

	UPROPERTY(EditAnywhere, Category = "Movement")
	float Radius = 75.f;

	UPROPERTY(EditAnywhere, Category = "Movement")
	float Height = 150.f;

	// ── 视野默认值 ──
	UPROPERTY(EditAnywhere, Category = "Vision")
	int32 SightRadius = 20;

	// ── 能力开关：按需勾选，决定这个单位挂哪些命令类Fragment ──
	UPROPERTY(EditAnywhere, Category = "Capabilities")
	bool bCanMove = true;

	UPROPERTY(EditAnywhere, Category = "Capabilities")
	bool bCanAttack = false;

	UPROPERTY(EditAnywhere, Category = "Capabilities")
	bool bIsInteractable = false;

	// 只有玩家控制的单位才能触发这个Entity的交互（勾选bIsInteractable时才生效）
	UPROPERTY(EditAnywhere, Category = "Capabilities", meta = (EditCondition = "bIsInteractable"))
	bool bPlayerOnlyInteraction = false;

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
