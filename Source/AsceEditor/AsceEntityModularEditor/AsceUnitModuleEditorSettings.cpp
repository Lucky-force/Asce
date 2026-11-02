// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/AsceUnitModuleEditorSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceUnitModuleEditorSettings)

UAsceUnitModuleEditorSettings::UAsceUnitModuleEditorSettings()
{
	ModuleConfigDirectory.Path = TEXT("/Game/DataTables/UnitModules");
	EntityTemplateDirectory.Path = TEXT("/Game/DataTables/UnitTemplates");
}
