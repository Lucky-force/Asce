// AsceBuildingTypes.cpp
#include "AsceMass/Building/AsceBuildingTypes.h"

FAsceBuildingPlacement FAsceBuildingPlacement::MakeFromDefCenter(
	FName InBuildingDefId,
	const FIntPoint& InSize,
	const FIntPoint& CenterCell,
	int32 InRotation,
	int32 InTeamKey)
{
	FAsceBuildingPlacement Out;
	Out.BuildingDefId = InBuildingDefId;
	Out.TeamKey = InTeamKey;
	Out.Rotation = ((InRotation % 4) + 4) % 4;
	Out.Size = FIntPoint(FMath::Max(1, InSize.X), FMath::Max(1, InSize.Y));

	// 中心格 → 未旋转 footprint 的最小角
	Out.OriginGridLocation = CenterCell - FIntPoint((Out.Size.X - 1) / 2, (Out.Size.Y - 1) / 2);
	return Out;
}
