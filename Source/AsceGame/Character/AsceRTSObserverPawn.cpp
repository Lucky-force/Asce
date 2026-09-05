// Fill out your copyright notice in the Description page of Project Settings.


#include "AsceRTSObserverPawn.h"

#include "AsceLogChannels.h"
#include "AsceObserverComponent.h"
#include "AscePawnExtensionComponent.h"
#include "Camera/AsceCameraComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/AscePlayerController.h"
#include "Player/AscePlayerState.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRTSObserverPawn)
AAsceRTSObserverPawn::AAsceRTSObserverPawn(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	
	SetNetCullDistanceSquared(900000000.0f);
	
	PawnExtComponent = CreateDefaultSubobject<UAscePawnExtensionComponent>(TEXT("PawnExtensionComponent"));
	
	//创建ASC初始化完成的回调
	// PawnExtComponent->OnAbilitySystemInitialized_RegisterAndCall(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnAbilitySystemInitialized));
	//创建ASC反初始化的回调
	// PawnExtComponent->OnAbilitySystemUninitialized_Register(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnAbilitySystemUninitialized));
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	CameraComponent = CreateDefaultSubobject<UAsceCameraComponent>(TEXT("CameraComponent"));
	CameraComponent->SetRelativeLocation(FVector(-300.0f, 0.0f, 75.0f));
	
	ObserverComponent = CreateDefaultSubobject<UAsceObserverComponent>(TEXT("ObserverComponent"));
}

AAscePlayerController* AAsceRTSObserverPawn::GetAscePlayerController() const
{
	return CastChecked<AAscePlayerController>(GetController(), ECastCheckedType::NullAllowed);
}

AAscePlayerState* AAsceRTSObserverPawn::GetAscePlayerState() const
{
	return CastChecked<AAscePlayerState>(GetPlayerState(), ECastCheckedType::NullAllowed);
}

void AAsceRTSObserverPawn::PreInitializeComponents()
{
	Super::PreInitializeComponents();
}

// Called when the game starts or when spawned
void AAsceRTSObserverPawn::BeginPlay()
{
	Super::BeginPlay();
	SetActorEnableCollision(false);
}

void AAsceRTSObserverPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void AAsceRTSObserverPawn::Reset()
{
	Super::Reset();
}

void AAsceRTSObserverPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, MyTeamID)
}

void AAsceRTSObserverPawn::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);
}

void AAsceRTSObserverPawn::NotifyControllerChanged()
{
	const FGenericTeamId OldTeamId = GetGenericTeamId();

	Super::NotifyControllerChanged();

	// Update our team ID based on the controller
	if (HasAuthority() && (GetController() != nullptr))
	{
		if (IAsceTeamAgentInterface* ControllerWithTeam = Cast<IAsceTeamAgentInterface>(GetController()))
		{
			MyTeamID = ControllerWithTeam->GetGenericTeamId();
			ConditionalBroadcastTeamChanged(this, OldTeamId, MyTeamID);
		}
	}
}

void AAsceRTSObserverPawn::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	if (GetController() == nullptr)
	{
		if (HasAuthority())
		{
			const FGenericTeamId OldTeamID = MyTeamID;
			MyTeamID = NewTeamID;
			ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
		}
		else
		{
			UE_LOG(LogAsceTeams, Error, TEXT("You can't set the team ID on a character (%s) except on the authority"), *GetPathNameSafe(this));
		}
	}
	else
	{
		UE_LOG(LogAsceTeams, Error, TEXT("You can't set the team ID on a possessed character (%s); it's driven by the associated controller"), *GetPathNameSafe(this));
	}
}

FGenericTeamId AAsceRTSObserverPawn::GetGenericTeamId() const
{
	return MyTeamID;
}

FOnAsceTeamIndexChangedDelegate* AAsceRTSObserverPawn::GetOnTeamIndexChangedDelegate()
{
	return &OnTeamChangedDelegate;
}

// Called to bind functionality to input
void AAsceRTSObserverPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PawnExtComponent->SetupPlayerInputComponent();
}

void AAsceRTSObserverPawn::FellOutOfWorld(const class UDamageType& dmgType)
{
	Super::FellOutOfWorld(dmgType);
}

void AAsceRTSObserverPawn::UninitAndDestroy()
{
	if (GetLocalRole() == ROLE_Authority)
	{
		DetachFromControllerPendingDestroy();
		SetLifeSpan(0.1f);
	}
	
	SetActorHiddenInGame(true);
}

void AAsceRTSObserverPawn::PossessedBy(AController* NewController)
{
	const FGenericTeamId OldTeamID = MyTeamID;

	Super::PossessedBy(NewController);

	//让PawnExtComponent处理Controller改变(包括让ASC刷一下ActorInfo，尝试初始化状态转移)
	PawnExtComponent->HandleControllerChanged();
	
	if (APlayerController* PlayerController = Cast<APlayerController>(NewController))
	{
		//修改这个Pawn的鼠标设置
		PlayerController->SetShowMouseCursor(true);
		PlayerController->bEnableClickEvents = true;
		PlayerController->bEnableMouseOverEvents = true;

		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		PlayerController->SetInputMode(InputMode);
	}
	
	// 绑定队伍改变的回调
	// Grab the current team ID and listen for future changes
	if (IAsceTeamAgentInterface* ControllerAsTeamProvider = Cast<IAsceTeamAgentInterface>(NewController))
	{
		MyTeamID = ControllerAsTeamProvider->GetGenericTeamId();
		ControllerAsTeamProvider->GetTeamChangedDelegateChecked().AddDynamic(this, &ThisClass::OnControllerChangedTeam);
	}
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
}

void AAsceRTSObserverPawn::UnPossessed()
{
	AController* const OldController = GetController();

	//移除队伍改变回调
	// Stop listening for changes from the old controller
	const FGenericTeamId OldTeamID = MyTeamID;
	if (IAsceTeamAgentInterface* ControllerAsTeamProvider = Cast<IAsceTeamAgentInterface>(OldController))
	{
		ControllerAsTeamProvider->GetTeamChangedDelegateChecked().RemoveAll(this);
	}

	Super::UnPossessed();

	PawnExtComponent->HandleControllerChanged();

	// Determine what the new team ID should be afterwards
	// 决定不再被Conroller占有后的队伍ID
	MyTeamID = DetermineNewTeamAfterPossessionEnds(OldTeamID);
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
}

void AAsceRTSObserverPawn::OnRep_Controller()
{
	Super::OnRep_Controller();
	PawnExtComponent->HandleControllerChanged();
}

void AAsceRTSObserverPawn::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	PawnExtComponent->HandlePlayerStateReplicated();
}

void AAsceRTSObserverPawn::OnControllerChangedTeam(UObject* TeamAgent, int32 OldTeam, int32 NewTeam)
{
	const FGenericTeamId MyOldTeamID = MyTeamID;
	MyTeamID = IntegerToGenericTeamId(NewTeam);
	ConditionalBroadcastTeamChanged(this, MyOldTeamID, MyTeamID);
}

void AAsceRTSObserverPawn::OnRep_MyTeamID(FGenericTeamId OldTeamID)
{
}

