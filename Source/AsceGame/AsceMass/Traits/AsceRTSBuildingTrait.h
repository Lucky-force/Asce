// AsceRTSBuildingTrait.h
#pragma once

#include "MassEntityTraitBase.h"
#include "AsceRTSBuildingTrait.generated.h"

/**
 * "建筑"专有 Trait（结构侧）：只加 `FAsceBuildingTag` 建筑标记。
 *
 * **footprint 和 Ether 标记不在这里加**——它们由 `UAsceBuildingSubsystem` 在生成后
 * 用 `AddFragmentInstanceListToEntity` 带**真实值**添加：
 * - footprint 随模板建会让 costfield/视野 observer 先按默认值触发一遍（之后改值不会再触发）；
 * - Ether 标记随模板建会让 AddNode 用模板 trait 里的类型 id 注册（每建筑的真实 id 后写，来不及）。
 *
 * 实体公共部分（Transform / 身份 / 血量 / NetID）挂 `UAsceRTSEntityTrait`；
 * 表现（模型）用 MassRepresentation 的 trait（如 `MassVisualizationTrait`）。
 */
UCLASS(BlueprintType, meta = (DisplayName = "Asce RTS Building"))
class UAsceRTSBuildingTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
