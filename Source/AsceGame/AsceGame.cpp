// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsceGame.h"
#include "Modules/ModuleManager.h"


class FAsceGameModule : public FDefaultGameModuleImpl
{
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, AsceGame, "AsceGame" );
