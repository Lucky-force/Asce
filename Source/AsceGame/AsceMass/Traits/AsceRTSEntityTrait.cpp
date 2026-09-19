// AsceRTSEntityTrait.cpp
#include "AsceRTSEntityTrait.h"

#include "MassEntityTemplateRegistry.h"
#include "MassCommonFragments.h"
#include "MassReplicationFragments.h"
#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "../Fragments/AsceCoreFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRTSEntityTrait)

void UAsceRTSEntityTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddFragment<FTransformFragment>();

	// 身份 id：行名必须是 0..65535 的数字（不查表，构建期零外部依赖）
	FAsceUnitDefFragment& DefFragment = BuildContext.AddFragment_GetRef<FAsceUnitDefFragment>();
	if (UniqueType.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EntityDef] %s：UniqueType 未配置（实体类型 id = 0）"), *GetName());
	}
	else if (!UAsceEntityDefLibrary::ParseEntityDefId(UniqueType.RowName, DefFragment.UniqueTypeId))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EntityDef] %s：UniqueType=%s 不是 0..65535 的数字行名（按 0 处理）"),
			*GetName(), *UniqueType.RowName.ToString());
	}

	// 阵营 fragment（结构）：值由生成方按归属写入（工厂 TeamKey 参数 / 建筑 placement），这里只给默认值
	BuildContext.AddFragment<FAsceTeamFragment>();

	if (bAddHealth)
	{
		FAsceHealthFragment& Health = BuildContext.AddFragment_GetRef<FAsceHealthFragment>();
		Health.MaxHealth = MaxHealth;
		Health.CurrentHealth = MaxHealth;
	}
	if (bAddNetworkID)
	{
		BuildContext.AddFragment<FMassNetworkIDFragment>();
	}

	BuildContext.AddTag<FAsceAliveTag>();
}
