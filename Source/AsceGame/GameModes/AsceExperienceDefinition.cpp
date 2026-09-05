// Fill out your copyright notice in the Description page of Project Settings.


#include "AsceExperienceDefinition.h"

UAsceExperienceDefinition::UAsceExperienceDefinition()
{
}

#if WITH_EDITOR
EDataValidationResult UAsceExperienceDefinition::IsDataValid(class FDataValidationContext& DataValidationContext) const
{
	return Super::IsDataValid(DataValidationContext);
}
#endif


#if WITH_EDITORONLY_DATA


void UAsceExperienceDefinition::UpdateAssetBundleData()
{
	Super::UpdateAssetBundleData();
}
#endif
