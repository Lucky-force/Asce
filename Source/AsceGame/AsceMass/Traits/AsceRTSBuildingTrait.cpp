// AsceRTSBuildingTrait.cpp
#include "AsceRTSBuildingTrait.h"

#include "MassEntityTemplateRegistry.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRTSBuildingTrait)

void UAsceRTSBuildingTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// ⚠️ **结构契约**：本 trait 只加"建筑标记"。Transform / 身份（`FAsceUnitDefFragment`）/ 阵营（`FAsceTeamFragment`）
	// 属于"实体基础"，由 `UAsceRTSEntityTrait` 提供（单位和建筑都要）⇒ 建筑的 `EntityTemplate`
	// **必须同时挂 `UAsceRTSEntityTrait`**；漏挂的话建筑会变成"半状态"：
	// costfield/视野/建筑层都正常（footprint 由建筑系统加），但 Ether 注册观察者不匹配 ⇒ 对 Ether 完全不可见
	// （创建时会有 `ensure` 报错，见 `UAsceBuildingSubsystem::CreateConstructingBuilding` / `UAsceEntityFactory`）。
	BuildContext.AddTag<FAsceBuildingTag>();
}
