// AsceEtherBuildingTrait.cpp
#include "AsceEtherBuildingTrait.h"

#include "MassEntityTemplateRegistry.h"
#include "AsceMass/Ether/AsceEtherBuildingData.h"
#include "AsceMass/Ether/AsceEtherFragments.h"
#include "AsceMass/Ether/AsceEtherNetworkSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceEtherBuildingTrait)

void UAsceEtherBuildingTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	const FName DefId = BuildingData
		? (BuildingDefId.IsNone() ? FName(*BuildingData->GetName()) : BuildingDefId)
		: BuildingDefId;

	// 登记建筑类型数据：Subsystem 按 BuildingDefId 反查（fragment 只留标记）
	if (BuildingData)
	{
		if (UAsceEtherNetworkSubsystem* Net = UAsceEtherNetworkSubsystem::Get(&World))
		{
			Net->RegisterBuildingData(DefId, BuildingData);
		}
	}

	// 标记 fragment：运行时注册/注销的锚点 + 身份
	BuildContext.AddFragment_GetRef<FEtherBuildingFragment>().BuildingDefId = DefId;

	// 器件：内部状态 fragment（时序单元的状态放这里，Subsystem 不持有）
	if (BuildingData && BuildingData->bIsDevice)
	{
		BuildContext.AddFragment_GetRef<FEtherDeviceStateFragment>();
	}
}
