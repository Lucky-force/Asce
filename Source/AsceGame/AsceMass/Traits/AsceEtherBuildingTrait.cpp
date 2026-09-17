// AsceEtherBuildingTrait.cpp
#include "AsceEtherBuildingTrait.h"

#include "MassEntityTemplateRegistry.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Ether/AsceEtherBuildingConfig.h"
#include "AsceMass/Ether/AsceEtherFragments.h"
#include "AsceMass/Ether/AsceEtherNetworkSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceEtherBuildingTrait)

void UAsceEtherBuildingTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	const FName DefId = BuildingConfig
		? (BuildingDefId.IsNone() ? BuildingConfig->GetFName() : BuildingDefId)
		: BuildingDefId;

	// 登记建筑配置：Subsystem 按 BuildingDefId 反查（fragment 只留标记）
	if (BuildingConfig)
	{
		if (UAsceEtherNetworkSubsystem* Net = UAsceEtherNetworkSubsystem::Get(&World))
		{
			Net->RegisterBuildingConfig(DefId, BuildingConfig);
		}
	}

	// 标记 fragment：运行时注册/注销的锚点 + 身份
	BuildContext.AddFragment_GetRef<FEtherBuildingFragment>().BuildingDefId = DefId;

	// 标记 tag：SpatialGrid "建筑层"按它收录（footprint 由放置方填：OriginGridLocation/Size/OcclusionHeightCm）
	BuildContext.AddTag<FAsceBuildingTag>();
}
