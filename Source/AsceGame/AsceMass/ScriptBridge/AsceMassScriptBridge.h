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

	/** [Legacy] 直接按模板生成（不写类型数据）；新代码请用 `SpawnEntitiesByDef`。 */
	UFUNCTION(BlueprintCallable, Category = "Mass|RTS")
	TArray<int64> SpawnEntities(UMassEntityConfigAsset* Config, const TArray<FTransform>& Transforms);

	/** **按实体定义生成**（内部走 `UAsceEntityFactory`；统一生成入口）。`TeamKey` = 归属阵营（生成方决定）。 */
	UFUNCTION(BlueprintCallable, Category = "Mass|RTS")
	TArray<int64> SpawnEntitiesByDef(FName DefRowName, const TArray<FTransform>& Transforms, int32 TeamKey = 0);

	UFUNCTION(BlueprintCallable, Category = "Mass|RTS")
	/** 销毁实体：参数是 `SpawnEntities*` 返回的 `int64` 句柄（`FMassEntityHandle::AsNumber()`）。
	 *  ⚠️ 必须是 `int64`：`AsNumber()` 是 `[Index(low32) | SerialNumber(high32)]` 的原始 64 位，
	 *  截成 int32 会丢掉 SerialNumber ⇒ `FromNumber` 得到的句柄 `IsEntityValid` 永远为假（拆不掉）。 */
	void DestroyEntities(const TArray<int64>& EntityHandles);

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
