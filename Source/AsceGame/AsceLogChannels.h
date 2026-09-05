// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#pragma once

#include "Logging/LogMacros.h"

class UObject;

ASCEGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogAsce, Log, All);
ASCEGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogAsceExperience, Log, All);
ASCEGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogAsceAbilitySystem, Log, All);
ASCEGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogAsceTeams, Log, All);

ASCEGAME_API FString GetClientServerContextString(UObject* ContextObject = nullptr);
