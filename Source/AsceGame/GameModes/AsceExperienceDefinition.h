// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine/DataAsset.h"
#include "AsceExperienceDefinition.generated.h"

class UAscePawnData;
class UGameFeatureAction;
class UAsceExperienceActionSet;

/**
 * 
 */
UCLASS(BlueprintType, Const)
class ASCEGAME_API UAsceExperienceDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
	
	UAsceExperienceDefinition();
	
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& DataValidationContext) const override;
#endif
	
#if WITH_EDITORONLY_DATA
	virtual void UpdateAssetBundleData() override;
#endif
	
public:
	
	UPROPERTY(EditDefaultsOnly, Category = "Gameplay")
	TObjectPtr<const UAscePawnData> DefaultPawnData;
	
	/**GameFeaturesToEanble 和 Actions加起来为一个ActionSet
	 * 额外提供ActionSet是为了方便使用一些预制的ActionSet来进行
	 * Experience的组合
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Gameplay")
	TArray<FString> GameFeaturesToEnable;
	
	UPROPERTY(EditDefaultsOnly, Category = "Action")
	TArray<TObjectPtr<UGameFeatureAction>> Actions;
	
	UPROPERTY(EditDefaultsOnly, Category = "Gameplay")
	TArray<TObjectPtr<UAsceExperienceActionSet>> ActionSets;
};
