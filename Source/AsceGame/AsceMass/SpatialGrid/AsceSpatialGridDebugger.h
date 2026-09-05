// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityHandle.h"
#include "UObject/Object.h"
#include "AsceSpatialGridDebugger.generated.h"

/**
 * 
 */
UCLASS()
class AAsceSpatialGridDebugger : public AActor
{
	GENERATED_BODY()
public:
	AAsceSpatialGridDebugger(const FObjectInitializer& ObjectInitializer);
	
	virtual void BeginPlay();
	
	UFUNCTION(BlueprintCallable)
	void DrawDebugGrid();
	
	void DrawDebugEntityBounds();
	
	virtual void Tick(float DeltaTime) override;
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DebugMID;
	
	UPROPERTY(EditAnywhere, Category = "Debug")
	TObjectPtr<UMaterialInterface> DebugDecalMaterial;
	
	UPROPERTY()
	TObjectPtr<UTexture2D> DebugTexture;

	UPROPERTY()
	TObjectPtr<UDecalComponent> DebugDecal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bDrawDebugGrid = false;
	
	UPROPERTY(EditAnywhere, Category = "Debug")
	float DebugDrawHeight = 10.f;
	
	UPROPERTY(EditAnywhere, Category = "Debug")
	int CellPixelSize = 3;
	
	float DebugRefreshTimer = 0.f;
	
	UPROPERTY(EditAnywhere, Category = "Debug")
	float DebugRefreshInterval = 1.f;
	
	UPROPERTY(EditAnywhere, Category = "Debug")
	int EntityDebugRange = 12;
	
	TSet<FMassEntityHandle> DrawnEntities;
	
	UPROPERTY(EditAnywhere, Category = "Debug")
	float DrawTime = 5.f;
};
