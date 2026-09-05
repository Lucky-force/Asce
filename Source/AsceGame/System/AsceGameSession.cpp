// Fill out your copyright notice in the Description page of Project Settings.


#include "AsceGameSession.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceGameSession)


AAsceGameSession::AAsceGameSession(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool AAsceGameSession::ProcessAutoLogin()
{
	// This is actually handled in AsceGameMode::TryDedicatedServerLogin
	return true;
}

void AAsceGameSession::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
}

void AAsceGameSession::HandleMatchHasEnded()
{
	Super::HandleMatchHasEnded();
}

