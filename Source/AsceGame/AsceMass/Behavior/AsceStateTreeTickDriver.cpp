// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AsceStateTreeTickDriver.h"

#include "Engine/World.h"
#include "MassCommonTypes.h"                 // UE::Mass::ProcessorGroupNames::Behavior
#include "MassEntityManager.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassSignalSubsystem.h"
#include "MassStateTreeFragments.h"
#include "MassStateTreeTypes.h"              // UE::Mass::Signals::NewStateTreeTaskRequired

#include "AsceMass/Behavior/AsceBehaviorFragment.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceStateTreeTickDriver)

UAsceStateTreeTickDriver::UAsceStateTreeTickDriver()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllNetModes);

	// 必须在行为组（树的 tick）之前：本帧发的信号本帧就能被树消费
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Behavior);
}

void UAsceStateTreeTickDriver::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddRequirement<FMassStateTreeInstanceFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAsceBehaviorFragment>(EMassFragmentAccess::ReadOnly);
}

void UAsceStateTreeTickDriver::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	if (!World)
	{
		return;
	}
	UMassSignalSubsystem* SignalSubsystem = World->GetSubsystem<UMassSignalSubsystem>();
	if (!SignalSubsystem)
	{
		return;
	}

	TArray<FMassEntityHandle> ActiveEntities;
	EntityQuery.ForEachEntityChunk(Context, [&ActiveEntities](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FAsceBehaviorFragment> Behaviors = Ctx.GetFragmentView<FAsceBehaviorFragment>();
		const TConstArrayView<FMassEntityHandle> Entities = Ctx.GetEntities();
		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			if (Behaviors[i].IsTimelineActive())
			{
				ActiveEntities.Add(Entities[i]);
			}
		}
	});

	if (ActiveEntities.Num() > 0)
	{
		SignalSubsystem->SignalEntities(UE::Mass::Signals::NewStateTreeTaskRequired, ActiveEntities);
	}
}
