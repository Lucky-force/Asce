// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "Containers/Ticker.h"
#include "GameUIManagerSubsystem.h"

#include "AsceUIManagerSubsystem.generated.h"

class FSubsystemCollectionBase;
class UObject;


//啥也没做，就是把PrimaryLayout（RootLayout）的显示和HUD绑定到一起了
UCLASS()
class UAsceUIManagerSubsystem : public UGameUIManagerSubsystem
{
	GENERATED_BODY()

public:

	UAsceUIManagerSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	bool Tick(float DeltaTime);
	void SyncRootLayoutVisibilityToShowHUD();
	
	FTSTicker::FDelegateHandle TickHandle;
};
