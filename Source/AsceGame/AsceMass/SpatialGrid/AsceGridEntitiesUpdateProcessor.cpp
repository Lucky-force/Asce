// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceGridEntitiesUpdateProcessor.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "../SpatialGrid/AsceSpatialGridSubsystem.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceGridEntitiesUpdateProcessor)

UAsceGridEntitiesUpdateProcessor::UAsceGridEntitiesUpdateProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = false;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
}

void UAsceGridEntitiesUpdateProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAsceHeightFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UAsceSpatialGridSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UAsceGridEntitiesUpdateProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ParallelForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FAgentRadiusFragment> RadiusList = Ctx.GetFragmentView<FAgentRadiusFragment>();
		const TConstArrayView<FMassEntityHandle> Handles = Ctx.GetEntities();
		const TConstArrayView<FAsceHeightFragment> Heights = Ctx.GetFragmentView<FAsceHeightFragment>();
		const int32 Num = Ctx.GetNumEntities();
		UAsceSpatialGridSubsystem& Grid = Ctx.GetMutableSubsystemChecked<UAsceSpatialGridSubsystem>();
		for (int32 i = 0; i < Num; ++i)
		{
			Grid.InsertEntity(Handles[i], Transforms[i].GetTransform().GetLocation(), RadiusList[i].Radius, Heights[i].Height);
		}
	});
}