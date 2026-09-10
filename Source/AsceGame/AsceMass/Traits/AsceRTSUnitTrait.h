#pragma once

#include "MassEntityTraitBase.h"
#include "AsceRTSUnitTrait.generated.h"

/**
 * "单位"类Entity（会动、会打、可能会死）的Fragment组装Trait。
 *
 * 按"是否会动"分Fragment组合，不用继承——通用属性(血量/阵营)始终挂，
 * 移动/攻击/交互能力通过下面的bCanXxx开关按需勾选，避免每个Entity都塞满不需要的Fragment。
 * 建筑类Entity（不会动，只挂FAsceGridFootprintFragment）不用这个Trait，另建
 * UAsceRTSBuildingTrait（下一轮再做）。
 */
UCLASS(BlueprintType, meta = (DisplayName = "Asce RTS Unit"))
class UAsceRTSUnitTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	// ── 移动 ──
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
	
	UPROPERTY(EditAnywhere, Category = "Vision")
	int32 SightRadius = 20;

	// ── 通用属性 ──
	UPROPERTY(EditAnywhere, Category = "Attributes")
	float MaxHealth = 100.f;

	// 打包的阵营键 (index<<16 | id)，写入 FAsceTeamFragment.TeamKey；默认 0 = 单队伍冒烟
	UPROPERTY(EditAnywhere, Category = "Attributes")
	int32 TeamConfigKey = 0;

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
