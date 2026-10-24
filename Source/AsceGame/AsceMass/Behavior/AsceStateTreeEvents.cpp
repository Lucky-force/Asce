// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AsceStateTreeEvents.h"

#include "Engine/World.h"
#include "MassEntityManager.h"
#include "MassSignalSubsystem.h"
#include "MassStateTreeFragments.h"
#include "MassStateTreeSubsystem.h"
#include "MassStateTreeTypes.h"      // UE::Mass::Signals::NewStateTreeTaskRequired
#include "StateTreeInstanceData.h"   // FStateTreeInstanceData::GetMutableEventQueue

namespace AsceStateTree
{
	bool SendEventToEntity(UWorld& World, FMassEntityManager& EntityManager, const FMassEntityHandle Entity
		, const FGameplayTag& Tag, const FConstStructView Payload, const FName Origin)
	{
		if (!Entity.IsValid() || !Tag.IsValid())
		{
			return false;
		}

		const FMassStateTreeInstanceFragment* TreeInstance = EntityManager.GetFragmentDataPtr<FMassStateTreeInstanceFragment>(Entity);
		if (!TreeInstance || !TreeInstance->InstanceHandle.IsValid())
		{
			return false;   // 该实体没有树（没配 StateTree / 还没激活）
		}

		UMassStateTreeSubsystem* StateTreeSubsystem = World.GetSubsystem<UMassStateTreeSubsystem>();
		if (!StateTreeSubsystem)
		{
			return false;
		}
		FStateTreeInstanceData* InstanceData = StateTreeSubsystem->GetInstanceData(TreeInstance->InstanceHandle);
		if (!InstanceData)
		{
			return false;
		}

		// ① 入队（Owner 只用于日志）
		if (!InstanceData->GetMutableEventQueue().SendEvent(StateTreeSubsystem, Tag, Payload, Origin))
		{
			return false;   // 队列满（MaxActiveEvents）等
		}

		// ② 唤醒：树是**信号驱动**的，不发这个信号事件会一直躺着等下一个外部信号
		if (UMassSignalSubsystem* SignalSubsystem = World.GetSubsystem<UMassSignalSubsystem>())
		{
			SignalSubsystem->SignalEntity(UE::Mass::Signals::NewStateTreeTaskRequired, Entity);
		}
		return true;
	}
}
