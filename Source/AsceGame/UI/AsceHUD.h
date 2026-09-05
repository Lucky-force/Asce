// Fill out your copyright notice in the Description page of Project Settings.


#pragma once

#include "GameFramework/HUD.h"

#include "AsceHUD.generated.h"

namespace EEndPlayReason { enum Type : int; }

class AActor;
class UObject;

/**
 * AAsceHUD
 *
 *  Note that you typically do not need to extend or modify this class, instead you would
 *  use an "Add Widget" action in your experience to add a HUD layout and widgets to it
 * 
 *  This class exists primarily for debug rendering
 *  
 *  只为了用于渲染显示debug信息（然而更好的方式依然是使用widget）, 通常ui都是使用widget
 */
UCLASS(Config = Game)
class AAsceHUD : public AHUD
{
	GENERATED_BODY()

public:
	AAsceHUD(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	//~UObject interface
	virtual void PreInitializeComponents() override;
	//~End of UObject interface

	//~AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~End of AActor interface

	//~AHUD interface
	virtual void GetDebugActorList(TArray<AActor*>& InOutList) override;
	//~End of AHUD interface
};
