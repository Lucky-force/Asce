// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/AsceUnitModuleConfig.h"

#include "MassEntityConfigAsset.h"

#include "AsceMass/Data/AsceEntityDefTypes.h"

#include "AsceEntityModularEditor/AsceUnitModuleGenerator.h"

UAsceUnitModuleConfig* UAsceUnitModuleConfig::LoadForDef(const FAsceEntityDefRow& Row)
{
#if WITH_EDITORONLY_DATA
	if (Row.ModuleConfig.IsNull())
	{
		return nullptr;
	}
	return Cast<UAsceUnitModuleConfig>(Row.ModuleConfig.TryLoad());
#else
	return nullptr;
#endif
}

bool UAsceUnitModuleConfig::ApplyToDef(FAsceEntityDefRow& Row, TArray<FText>& OutErrors)
{
	FAsceUnitModuleBuildResult Result = AsceUnitModuleGenerator::Build(Modules);
	if (!Result.IsValid())
	{
		OutErrors.Append(Result.Errors);
		return false;
	}

	UMassEntityConfigAsset* TemplateAsset = Row.EntityTemplate.LoadSynchronous();
	if (!TemplateAsset)
	{
		OutErrors.Add(INVTEXT("def 行没有配置 EntityTemplate（模板资产）"));
		return false;
	}

	if (!AsceUnitModuleGenerator::WriteToTemplateAsset(TemplateAsset, Result, OutErrors))
	{
		return false;
	}
	if (!AsceUnitModuleGenerator::WriteToDefRow(Modules, Row, OutErrors))
	{
		return false;
	}

	SourceHash = Result.SourceHash;
	MarkPackageDirty();
	return true;
}

bool UAsceUnitModuleConfig::IsDirty() const
{
	FAsceUnitModuleBuildResult Result = AsceUnitModuleGenerator::Build(Modules);
	return Result.SourceHash != SourceHash;
}
