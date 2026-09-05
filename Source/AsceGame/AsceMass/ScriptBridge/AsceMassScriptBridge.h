#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MassEntityManager.h"
#include "AsceMassScriptBridge.generated.h"

#define UE_API ASCEGAME_API

class UAsceSpatialGridSubsystem;
class UMassEntitySubsystem;
class UMassSpawnerSubsystem;
class UMassEntityConfigAsset;

UCLASS(MinimalAPI, BlueprintType)
class UAsceMassScriptBridge : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//WorldSubsystem Interface
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~WorldSubsystem Interface
	
	
	UFUNCTION(BlueprintCallable, Category = "Mass|RTS")
	UAsceMassScriptBridge* Get(UObject* ContextObject);
	
	// ── 生成 / 销毁 ──────────────────────────────────
	
	UFUNCTION(BlueprintCallable, Category = "Mass|RTS")
	TArray<int64> SpawnEntities(UMassEntityConfigAsset* Config, const TArray<FTransform>& Transforms);

	UFUNCTION(BlueprintCallable, Category = "Mass|RTS")
	void DestroyEntities(const TArray<int32>& EntityHandles);

	// ── 命令 ────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "Mass|RTS")
	void MoveOrder(FVector TargetLocation, float AcceptanceRadius = 50.f);


protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UPROPERTY()
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY()
	TObjectPtr<UMassSpawnerSubsystem> SpawnerSubsystem;
	
	UPROPERTY()
	TObjectPtr<UAsceSpatialGridSubsystem> SpatialGridSubsystem;
};

#undef UE_API
