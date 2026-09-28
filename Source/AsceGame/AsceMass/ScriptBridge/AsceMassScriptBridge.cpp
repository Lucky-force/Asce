#include "AsceMassScriptBridge.h"

#include "MassEntitySubsystem.h"
#include "MassSpawnerSubsystem.h"
#include "MassEntityConfigAsset.h"
#include "MassEntityTemplate.h"
#include "MassCommonFragments.h"
#include "MassEntityTypes.h"
#include "MassExecutionContext.h"
#include "Engine/World.h"
#include "AsceMass/Data/AsceEntityFactory.h"
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
	// 给了 ContextObject 就用它的世界（脚本里常传 `this`/Actor ⇔ 原来被忽略，跨世界取错子系统）；
	// 没给则退回本对象所在世界（保持原行为）。
	UWorld* World = ContextObject ? ContextObject->GetWorld() : GetWorld();
	if (IsValid(World))
	{
		return World->GetSubsystem<UAsceMassScriptBridge>();
	}
	return nullptr;
}

// ── 生成 / 销毁 ────────────────────────────────────

TArray<int64> UAsceMassScriptBridge::SpawnEntities(UMassEntityConfigAsset* Config, const TArray<FTransform>& Transforms)
{
	UE_LOG(LogTemp, Warning, TEXT("[MassBridge] SpawnEntities(Config, ...) 已弃用：它不查实体定义表，fragment 全是 trait 默认值（半径/速度/血量等不会按表覆盖）。请改用 SpawnEntitiesByDef(DefRowName, Transforms, TeamId)"));

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

TArray<int64> UAsceMassScriptBridge::SpawnEntitiesByDef(FName DefRowName, const TArray<FTransform>& Transforms, int32 TeamId)
{
	return UAsceEntityFactory::SpawnEntitiesByDef(this, DefRowName, Transforms, TeamId);
}

void UAsceMassScriptBridge::DestroyEntities(const TArray<int64>& EntityHandles){
	if (!IsValid(EntitySubsystem))
	{
		return;
	}

	// **销毁必须服务器权威**：客户端本地销毁不会走复制（服务器/别的端还留着，变成各端不一致的幽灵）
	const UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MassBridge] DestroyEntities 只能在权威端调用（当前为 NM_Client），已忽略：%d 个句柄"),
			EntityHandles.Num());
		return;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

	// 句柄是 `AsNumber()` 的原始 64 位（Index 低 32 + SerialNumber 高 32）⇒ 必须用 int64 还原
	for (const int64 EntityNumber : EntityHandles)
	{
		const FMassEntityHandle Handle = FMassEntityHandle::FromNumber(static_cast<uint64>(EntityNumber));
		if (EntityManager.IsEntityValid(Handle))
		{
			EntityManager.DestroyEntity(Handle);
		}
	}
}

