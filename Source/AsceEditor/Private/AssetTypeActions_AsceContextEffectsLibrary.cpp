// Fill out your copyright notice in the Description page of Project Settings.


#include "AssetTypeActions_AsceContextEffectsLibrary.h"

#include "Feedback/ContextEffects/AsceContextEffectsLibrary.h"

 class UClass;

 #define LOCTEXT_NAMESPACE "AssetTypeActions"

 UClass* FAssetTypeActions_AsceContextEffectsLibrary::GetSupportedClass() const
 {
 	return UAsceContextEffectsLibrary::StaticClass();
 }

 #undef LOCTEXT_NAMESPACE