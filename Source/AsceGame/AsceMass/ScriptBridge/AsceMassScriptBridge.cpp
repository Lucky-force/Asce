#include "AsceMassScriptBridge.h"

#include "MassEntitySubsystem.h"
#include "MassSpawnerSubsystem.h"
#include "MassEntityConfigAsset.h"
#include "MassEntityTemplate.h"
#include "MassCommonFragments.h"
#include "MassEntityTypes.h"
#include "MassExecutionContext.h"
#include "Engine/World.h"
#include "AsceMass/SpatialGrid/AsceSpatialGridSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceMassScriptBridge)

void UAsceMassScriptBridge::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAsceMassScriptBridge::Deinitialize()
{
	EntitySubsystem = nullptr;
	SpawnerSubsystem = nullptr;
	Super::Deinitialize();
}

void UAsceMassScriptBridge::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	EntitySubsystem = InWorld.GetSubsystem<UMassEntitySubsystem>();
	SpawnerSubsystem = InWorld.GetSubsystem<UMassSpawnerSubsystem>();
	SpatialGridSubsystem = InWorld.GetSubsystem<UAsceSpatialGridSubsystem>();
}

UAsceMassScriptBridge* UAsceMassScriptBridge::Get(UObject* ContextObject)
{
	UWorld* World = GetWorld();
	if (IsValid(World))
	{
		return World->GetSubsystem<UAsceMassScriptBridge>();
	}
	return nullptr;
}

// ── 生成 / 销毁 ────────────────────────────────────

TArray<int64> UAsceMassScriptBridge::SpawnEntities(UMassEntityConfigAsset* Config, const TArray<FTransform>& Transforms)
{
	TArray<int64> Result;
	if (!Config || !IsValid(SpawnerSubsystem) || Transforms.Num() == 0 || !IsValid(SpatialGridSubsystem) || !IsValid(EntitySubsystem))
	{
		return Result;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return Result;
	}

	const FMassEntityTemplate& EntityTemplate = Config->GetOrCreateEntityTemplate(*World);
	if (!EntityTemplate.IsValid())
	{
		return Result;
	}

	TArray<FMassEntityHandle> SpawnedHandles;
	SpawnerSubsystem->SpawnEntities(EntityTemplate, Transforms.Num(), SpawnedHandles);
	
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	// 设置初始位置
	if (SpawnedHandles.Num() > 0)
	{
		for (int32 i = 0; i < SpawnedHandles.Num() && i < Transforms.Num(); ++i)
		{
			if (EntityManager.IsEntityValid(SpawnedHandles[i]))
			{
				const FTransformFragment TransformFragment(Transforms[i]);
				EntityManager.GetFragmentDataChecked<FTransformFragment>(SpawnedHandles[i]).SetTransform(Transforms[i]);
			}
		}
	}

	for (const FMassEntityHandle& Handle : SpawnedHandles)
	{
		Result.Add(Handle.AsNumber());
	}
	return Result;
}

void UAsceMassScriptBridge::DestroyEntities(const TArray<int32>& EntityHandles)
{
	if (!IsValid(EntitySubsystem))
	{
		return;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

	for (int32 Index : EntityHandles)
	{
		FMassEntityHandle Handle = FMassEntityHandle::FromNumber(Index);
		if (EntityManager.IsEntityValid(Handle))
		{
			EntityManager.DestroyEntity(Handle);
		}
	}
}

void UAsceMassScriptBridge::MoveOrder(FVector TargetLocation, float AcceptanceRadius)
 {
 }
