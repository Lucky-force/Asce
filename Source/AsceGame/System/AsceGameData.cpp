// Fill out your copyright notice in the Description page of Project Settings.


#include "AsceGameData.h"
#include "AsceAssetManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceGameData)

UAsceGameData::UAsceGameData()
{
}

const UAsceGameData& UAsceGameData::UAsceGameData::Get()
{
	return UAsceAssetManager::Get().GetGameData();
}
