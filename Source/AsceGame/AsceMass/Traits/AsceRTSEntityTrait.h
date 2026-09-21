// AsceRTSEntityTrait.h
#pragma once

#include "MassEntityTraitBase.h"
#include "DataTableSystem/AsceRowNameWrapper.h"
#include "AsceRTSEntityTrait.generated.h"

/**
 * 实体**公共核心** Trait（单位 / 建筑都挂）：
 * - 身份：`FAsceUnitDefFragment`（`UniqueType` = 实体定义表行名数字，跨 session/两端一致）；
 * - 公共部分：Transform / 阵营 / 血量（可关） / NetID（可关） / AliveTag。
 *
 * 单位专有内容见 `UAsceRTSUnitTrait`（移动/视野/订单/能力），建筑见 `UAsceRTSBuildingTrait`
 * （footprint/建筑标记）；MassEntityConfig 里按需组合。
 */
UCLASS(BlueprintType, meta = (DisplayName = "Asce RTS Entity"))
class UAsceRTSEntityTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	/** 实体类型（`AsceEntityDefRow` 行名 = 数字 ID；下拉选行）。 */
	UPROPERTY(EditAnywhere, Category = "Definition",
		meta = (DataTableBinding = "AsceEntityDefRow:DisplayName,Description"))
	FAsceRowNameWrapper UniqueType;

	// 注意：**阵营不在这里配**——归属取决于谁生成/建造，由生成方在创建时写入
	// （单位走 `UAsceEntityFactory` 的 TeamKey 参数；建筑走 `FAsceBuildingPlacement::TeamKey`）。

	UPROPERTY(EditAnywhere, Category = "Attributes")
	bool bAddHealth = true;

	UPROPERTY(EditAnywhere, Category = "Attributes", meta = (EditCondition = "bAddHealth"))
	float MaxHealth = 100.f;

	/** 联机同步用的 NetID（服务器权威分配，见 `UAsceNetIDRegistrySubsystem`）。 */
	UPROPERTY(EditAnywhere, Category = "Replication")
	bool bAddNetworkID = true;

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
