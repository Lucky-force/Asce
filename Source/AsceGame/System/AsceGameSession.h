// Fill out your copyright notice in the Description page of Project Settings.

#pragma once


#include "GameFramework/GameSession.h"

#include "AsceGameSession.generated.h"

class UObject;


UCLASS(Config = Game)
class AAsceGameSession : public AGameSession
{
	GENERATED_BODY()

public:

	AAsceGameSession(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	/** Override to disable the default behavior */
	virtual bool ProcessAutoLogin() override;

	virtual void HandleMatchHasStarted() override;
	virtual void HandleMatchHasEnded() override;
};
