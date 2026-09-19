// AsceRTSBuildingTrait.cpp
#include "AsceRTSBuildingTrait.h"

#include "MassEntityTemplateRegistry.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRTSBuildingTrait)

void UAsceRTSBuildingTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddTag<FAsceBuildingTag>();
}
