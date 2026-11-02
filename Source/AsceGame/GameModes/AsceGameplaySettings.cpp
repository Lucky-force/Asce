// Fill out your copyright notice in the Description page of Project Settings.

#include "GameModes/AsceGameplaySettings.h"

#include "MassRepresentationActorManagement.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceGameplaySettings)

UAsceGameplaySettings::UAsceGameplaySettings()
{
	for (int32 LOD = 0; LOD < EMassLOD::Max; ++LOD)
	{
		UnitRepresentationParams.LODRepresentation[LOD] = (LOD == EMassLOD::Off)
			? EMassRepresentationType::None
			: EMassRepresentationType::StaticMeshInstance;
	}

	UnitRepresentationParams.RepresentationActorManagementClass = UMassRepresentationActorManagement::StaticClass();
}