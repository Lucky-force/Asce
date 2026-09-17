// AsceBuildingTypes.cpp
#include "AsceMass/Building/AsceBuildingTypes.h"

#include "AsceMass/Ether/AsceEtherBuildingConfig.h"

FAsceBuildingPlacement FAsceBuildingPlacement::MakeFromConfigCenter(
	TSubclassOf<UAsceEtherBuildingConfig> InConfig,
	const FIntPoint& CenterCell,
	int32 InRotation,
	int32 InTeamKey,
	FName InBuildingDefId)
{
	FAsceBuildingPlacement Out;
	Out.BuildingConfig = InConfig;
	Out.BuildingDefId = InBuildingDefId;
	Out.TeamKey = InTeamKey;
	Out.Rotation = ((InRotation % 4) + 4) % 4;

	const UAsceEtherBuildingConfig* Cfg = InConfig ? InConfig.GetDefaultObject() : nullptr;
	Out.Size = Cfg
		? FIntPoint(FMath::Max(1, Cfg->SizeX), FMath::Max(1, Cfg->SizeY))
		: FIntPoint(1, 1);

	// 中心格 → 未旋转 footprint 的最小角
	Out.OriginGridLocation = CenterCell - FIntPoint((Out.Size.X - 1) / 2, (Out.Size.Y - 1) / 2);
	return Out;
}
