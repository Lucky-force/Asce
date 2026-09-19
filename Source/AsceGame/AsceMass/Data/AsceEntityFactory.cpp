// AsceEntityFactory.cpp
#include "AsceEntityFactory.h"

#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "AsceMass/Data/AsceUnitDef.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Fragments/AsceMovementFragments.h"
#include "AsceMass/Vision/AsceVisionFragments.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityTemplate.h"
#include "MassSpawnerSubsystem.h"

namespace
{
	/** 把单位类型数据写进已生成的实体（有哪些 fragment 就写哪些）。 */
	void ApplyUnitDef(FMassEntityManager& EntityManager, FMassEntityHandle Entity, const FAsceRTSUnitDef& Def, int32 TeamKey)
	{
		if (FAsceHealthFragment* Health = EntityManager.GetFragmentDataPtr<FAsceHealthFragment>(Entity))
		{
			Health->MaxHealth = Def.MaxHealth;
			Health->CurrentHealth = Def.MaxHealth;
		}
		if (FAsceTeamFragment* Team = EntityManager.GetFragmentDataPtr<FAsceTeamFragment>(Entity))
		{
			Team->TeamKey = TeamKey;
		}
		if (FAgentRadiusFragment* Radius = EntityManager.GetFragmentDataPtr<FAgentRadiusFragment>(Entity))
		{
			Radius->Radius = Def.Radius;
		}
		if (FAsceHeightFragment* Height = EntityManager.GetFragmentDataPtr<FAsceHeightFragment>(Entity))
		{
			Height->Height = Def.Height;
		}
		if (FAsceMovementTuningFragment* Tuning = EntityManager.GetFragmentDataPtr<FAsceMovementTuningFragment>(Entity))
		{
			Tuning->MaxSpeed = Def.MaxSpeed;
			Tuning->MaxAcceleration = Def.MaxAcceleration;
			Tuning->DefaultDesiredSpeed = Def.DefaultDesiredSpeed;
		}
		if (FAsceVisionViewFragment* View = EntityManager.GetFragmentDataPtr<FAsceVisionViewFragment>(Entity))
		{
			View->SightRadius = static_cast<uint16>(FMath::Clamp(Def.SightRadius, 1, static_cast<int32>(MAX_uint16)));
			View->EyeHeightCm = static_cast<uint16>(FMath::Clamp(Def.Height, 0.f, static_cast<float>(MAX_uint16)));
		}
	}
}

TArray<int64> UAsceEntityFactory::SpawnEntitiesByDef(UObject* WorldContextObject, FName DefRowName, const TArray<FTransform>& Transforms, int32 TeamKey)
{
	TArray<int64> Result;
	if (Transforms.Num() == 0)
	{
		return Result;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return Result;
	}

	FAsceEntityDefRow Row;
	if (!UAsceEntityDefLibrary::GetEntityDefRowByName(DefRowName, Row))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EntityFactory] 实体定义表里没有行 '%s'"), *DefRowName.ToString());
		return Result;
	}

	UMassEntityConfigAsset* Config = Row.EntityTemplate.LoadSynchronous();
	if (!Config)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EntityFactory] 行 '%s' 没配 EntityTemplate（生成模板）"), *DefRowName.ToString());
		return Result;
	}

	UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	UMassSpawnerSubsystem* SpawnerSubsystem = World->GetSubsystem<UMassSpawnerSubsystem>();
	if (!MassSubsystem || !SpawnerSubsystem)
	{
		return Result;
	}

	const FMassEntityTemplate& EntityTemplate = Config->GetOrCreateEntityTemplate(*World);
	if (!EntityTemplate.IsValid())
	{
		return Result;
	}

	const FAsceRTSUnitDef* UnitDef = Row.Def.GetPtr<FAsceRTSUnitDef>();
	if (!UnitDef)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EntityFactory] 行 '%s' 的 Def 不是 FAsceRTSUnitDef（工厂目前只支持单位）"), *DefRowName.ToString());
	}

	TArray<FMassEntityHandle> SpawnedHandles;
	SpawnerSubsystem->SpawnEntities(EntityTemplate, Transforms.Num(), SpawnedHandles);

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	for (int32 i = 0; i < SpawnedHandles.Num(); ++i)
	{
		const FMassEntityHandle Handle = SpawnedHandles[i];
		if (!EntityManager.IsEntityValid(Handle))
		{
			continue;
		}
		if (Transforms.IsValidIndex(i))
		{
			if (FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Handle))
			{
				Transform->SetTransform(Transforms[i]);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[EntityFactory] 模板缺少 FTransformFragment（模板要挂 UAsceRTSEntityTrait）"));
			}
		}
		if (UnitDef)
		{
			ApplyUnitDef(EntityManager, Handle, *UnitDef, TeamKey);
		}
		Result.Add(Handle.AsNumber());
	}
	return Result;
}
