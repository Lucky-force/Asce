// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MassRepresentationFragments.h"
#include "MassRepresentationTypes.h"

#include "AsceGameplaySettings.generated.h"

class UStateTree;
class AActor;

/**
 * Global gameplay defaults, configurable in Project Settings -> Game -> Asce Gameplay
 * (Config/DefaultGame.ini, [/Script/AsceGame.AsceGameplaySettings]).
 *
 * Unit visualization defaults are applied to every unit's MassMovableVisualizationTrait
 * when the module editor writes templates (or via the "apply global LOD" button).
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Asce Gameplay"))
class ASCEGAME_API UAsceGameplaySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAsceGameplaySettings();

	/** Default unit state tree, used when a unit def has no StateTree. */
	UPROPERTY(EditDefaultsOnly, config, Category = "Tree", meta = (AllowedClasses = "/Script/StateTreeModule.StateTree"))
	TSoftObjectPtr<UStateTree> DefaultUnitStateTree;

	/** Default melee attack tree. */
	UPROPERTY(EditDefaultsOnly, config, Category = "Tree", meta = (AllowedClasses = "/Script/StateTreeModule.StateTree"))
	TSoftObjectPtr<UStateTree> DefaultMeleeAttackTree;

	/** Default ranged attack tree. */
	UPROPERTY(EditDefaultsOnly, config, Category = "Tree", meta = (AllowedClasses = "/Script/StateTreeModule.StateTree"))
	TSoftObjectPtr<UStateTree> DefaultRangedAttackTree;

	/** Override each unit template's MassMovableVisualizationTrait with the settings below. */
	UPROPERTY(EditDefaultsOnly, config, Category = "Unit|Visualization")
	bool bOverrideUnitVisualizationLOD = true;

	/** Full FMassRepresentationParameters: LOD representation, update rate, actor settings, etc. */
	UPROPERTY(EditDefaultsOnly, config, Category = "Unit|Visualization", meta = (EditCondition = "bOverrideUnitVisualizationLOD", DisplayName = "Representation Parameters"))
	FMassRepresentationParameters UnitRepresentationParams;

	/** Full FMassVisualizationLODParameters: Base/Visible LOD distances, max counts, frustum settings, etc. */
	UPROPERTY(EditDefaultsOnly, config, Category = "Unit|Visualization", meta = (EditCondition = "bOverrideUnitVisualizationLOD", DisplayName = "LOD Parameters"))
	FMassVisualizationLODParameters UnitVisualizationLODParams;

	UPROPERTY(EditDefaultsOnly, config, Category = "Unit|Visualization", meta = (EditCondition = "bOverrideUnitVisualizationLOD"))
	TSubclassOf<AActor> HighResTemplateActor;

	UPROPERTY(EditDefaultsOnly, config, Category = "Unit|Visualization", meta = (EditCondition = "bOverrideUnitVisualizationLOD"))
	TSubclassOf<AActor> LowResTemplateActor;

	UPROPERTY(EditDefaultsOnly, config, Category = "Unit|Visualization", meta = (EditCondition = "bOverrideUnitVisualizationLOD"))
	bool bAllowServerSideVisualization = false;

	virtual FName GetCategoryName() const override { return FName(TEXT("Asce")); }
};
