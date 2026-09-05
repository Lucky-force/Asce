// Fill out your copyright notice in the Description page of Project Settings.

#include "AscePlayerController.h"
#include "CommonInputTypeEnum.h"
#include "Components/PrimitiveComponent.h"
#include "AsceLogChannels.h"
#include "AsceCheatManager.h"
#include "AscePlayerState.h"
#include "Camera/AscePlayerCameraManager.h"
#include "UI/AsceHUD.h"
#include "AbilitySystem/AsceAbilitySystemComponent.h"
#include "EngineUtils.h"
#include "AsceGameplayTags.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Engine/GameInstance.h"
#include "AbilitySystemGlobals.h"
#include "CommonInputSubsystem.h"
#include "AsceLocalPlayer.h"
#include "GameModes/AsceGameState.h"
#include "Settings/AsceSettingsLocal.h"
#include "Settings/AsceSettingsShared.h"
//#include "Replays/AsceReplaySubsystem.h"
#include "ReplaySubsystem.h"
#include "Development/AsceDeveloperSettings.h"
#include "GameMapsSettings.h"
#if WITH_RPC_REGISTRY
#include "Tests/AsceGameplayRpcRegistrationComponent.h"
#include "HttpServerModule.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(AscePlayerController)

namespace Asce
{
	namespace Input
	{
		static int32 ShouldAlwaysPlayForceFeedback = 0;
		static FAutoConsoleVariableRef CVarShouldAlwaysPlayForceFeedback(TEXT("AscePC.ShouldAlwaysPlayForceFeedback"),
			ShouldAlwaysPlayForceFeedback,
			TEXT("Should force feedback effects be played, even if the last input device was not a gamepad?"));
	}
}



AAscePlayerController::AAscePlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	//CameraManager类设置
	PlayerCameraManagerClass = AAscePlayerCameraManager::StaticClass();

#if USING_CHEAT_MANAGER
	CheatClass = UAsceCheatManager::StaticClass();
#endif // #if USING_CHEAT_MANAGER
}

void AAscePlayerController::PreInitializeComponents()
{
	Super::PreInitializeComponents();
}

void AAscePlayerController::BeginPlay()
{
	Super::BeginPlay();
	#if WITH_RPC_REGISTRY
	FHttpServerModule::Get().StartAllListeners();
	int32 RpcPort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("rpcport="), RpcPort))
	{
		UAsceGameplayRpcRegistrationComponent* ObjectInstance = UAsceGameplayRpcRegistrationComponent::GetInstance();
		if (ObjectInstance && ObjectInstance->IsValidLowLevel())
		{
			//接受远端网络调试
			ObjectInstance->RegisterAlwaysOnHttpCallbacks();
			ObjectInstance->RegisterInMatchHttpCallbacks();
		}
	}
	#endif
	SetActorHiddenInGame(false);
}

void AAscePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void AAscePlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Disable replicating the PC target view as it doesn't work well for replays or client-side spectating.
	// The engine TargetViewRotation is only set in APlayerController::TickActor if the server knows ahead of time that 
	// a specific pawn is being spectated and it only replicates down for COND_OwnerOnly.
	// In client-saved replays, COND_OwnerOnly is never true and the target pawn is not always known at the time of recording.
	// To support client-saved replays, the replication of this was moved to ReplicatedViewRotation and updated in PlayerTick.
	// 禁止复制控制器的target view， 此数据在客户端的观察者模式回放中效果很差， TargetViewRotation只在服务器预先知道一个Pawn正在观察时，在TickActor中设置，
	// 并且只会复制到拥有者所在连接的客户端。在客户端保存的回放中，COND_OwnerOnly永远不为真，并且在记录回放的时候并不知道目标的pawn
	// 为了支持客户端保存回放，这个值的复制被移动到了ReplicatedViewRotation中并且在PlayerTick中更新
	DISABLE_REPLICATED_PROPERTY(APlayerController, TargetViewRotation);
}

void AAscePlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
}

void AAscePlayerController::PlayerTick(float DeltaTime)
{
	//仅仅在这个controller接受玩家的输入时才会调用这个tick用来处理玩家特定的逻辑
	Super::PlayerTick(DeltaTime);

	// If we are auto running then add some player input
	// 如果是自动奔跑，每一帧都输入一下移动
	if (GetIsAutoRunning())
	{
		if (APawn* CurrentPawn = GetPawn())
		{
			const FRotator MovementRotation(0.0f, GetControlRotation().Yaw, 0.0f);
			const FVector MovementDirection = MovementRotation.RotateVector(FVector::ForwardVector);
			CurrentPawn->AddMovementInput(MovementDirection, 1.0f);	
		}
	}

	AAscePlayerState* AscePlayerState = GetAscePlayerState();

	if (PlayerCameraManager && AscePlayerState)
	{
		APawn* TargetPawn = PlayerCameraManager->GetViewTargetPawn();

		if (TargetPawn)
		{
			// Update view rotation on the server so it replicates
			// 在服务器上获取当前控制的Pawn的ViewRotation并设置到PlayerState中，随后复制到客户端
			if (HasAuthority() || TargetPawn->IsLocallyControlled())
			{
				AscePlayerState->SetReplicatedViewRotation(TargetPawn->GetViewRotation());
			}

			// Update the target view rotation if the pawn isn't locally controlled
			// 非服务器时，从PlayerState中的复制值取出来设置到自己的TargetViewRotation上
			if (!TargetPawn->IsLocallyControlled())
			{
				AscePlayerState = TargetPawn->GetPlayerState<AAscePlayerState>();
				if (AscePlayerState)
				{
					// Get it from the spectated pawn's player state, which may not be the same as the PC's playerstate
					TargetViewRotation = AscePlayerState->GetReplicatedViewRotation();
				}
			}
		}
	}
}

AAscePlayerState* AAscePlayerController::GetAscePlayerState() const
{
	return CastChecked<AAscePlayerState>(PlayerState, ECastCheckedType::NullAllowed);
}

UAsceAbilitySystemComponent* AAscePlayerController::GetAsceAbilitySystemComponent() const
{
	const AAscePlayerState* AscePS = GetAscePlayerState();
	return (AscePS ? AscePS->GetAsceAbilitySystemComponent() : nullptr);
}

AAsceHUD* AAscePlayerController::GetAsceHUD() const
{
	return CastChecked<AAsceHUD>(GetHUD(), ECastCheckedType::NullAllowed);
}

bool AAscePlayerController::TryToRecordClientReplay()
{
	// See if we should record a replay
	// if (ShouldRecordClientReplay())
	// {
	// 	if (UAsceReplaySubsystem* ReplaySubsystem = GetGameInstance()->GetSubsystem<UAsceReplaySubsystem>())
	// 	{
	// 		APlayerController* FirstLocalPlayerController = GetGameInstance()->GetFirstLocalPlayerController();
	// 		if (FirstLocalPlayerController == this)
	// 		{
	// 			// If this is the first player, update the spectator player for local replays and then record
	// 			// 记录客户端的回放，必须保证是第一个玩家controller，即该客户端的可操控代理
	// 			if (AAsceGameState* GameState = Cast<AAsceGameState>(GetWorld()->GetGameState()))
	// 			{
	// 				GameState->SetRecorderPlayerState(PlayerState);
	//
	// 				ReplaySubsystem->RecordClientReplay(this);
	// 				return true;
	// 			}
	// 		}
	// 	}
	// }
	 return false;
}

bool AAscePlayerController::ShouldRecordClientReplay()
{
// 	UWorld* World = GetWorld();
// 	UGameInstance* GameInstance = GetGameInstance();
// 	if (GameInstance != nullptr &&
// 		World != nullptr &&
// 		!World->IsPlayingReplay() &&
// 		!World->IsRecordingClientReplay() &&
// 		NM_DedicatedServer != GetNetMode() &&
// 		IsLocalPlayerController())
// 	{
// 		FString DefaultMap = UGameMapsSettings::GetGameDefaultMap();
// 		FString CurrentMap = World->URL.Map;
//
// #if WITH_EDITOR
// 		CurrentMap = UWorld::StripPIEPrefixFromPackageName(CurrentMap, World->StreamingLevelsPrefix);
// #endif
// 		if (CurrentMap == DefaultMap)
// 		{
// 			// Never record demos on the default frontend map, this could be replaced with a better check for being in the main menu
// 			// 检查当前的地图是不是default map， 如果是，则禁止录像
// 			return false;
// 		}
//
// 		if (UReplaySubsystem* ReplaySubsystem = GameInstance->GetSubsystem<UReplaySubsystem>())
// 		{
// 			if (ReplaySubsystem->IsRecording() || ReplaySubsystem->IsPlaying())
// 			{
// 				// Only one at a time
// 				// 同一时间只能记录一个
// 				return false;
// 			}
// 		}
//
// 		// If this is possible, now check the settings
// 		// 走到此处，检查一下本地设置
// 		if (const UAsceLocalPlayer* AsceLocalPlayer = Cast<UAsceLocalPlayer>(GetLocalPlayer()))
// 		{
// 			if (AsceLocalPlayer->GetLocalSettings()->ShouldAutoRecordReplays())
// 			{
// 				return true;
// 			}
// 		}
// 	}
 	return false;
}

void AAscePlayerController::OnPlayerStateChangedTeam(UObject* TeamAgent, int32 OldTeam, int32 NewTeam)
{
	ConditionalBroadcastTeamChanged(this, IntegerToGenericTeamId(OldTeam), IntegerToGenericTeamId(NewTeam));
}

void AAscePlayerController::OnPlayerStateChanged()
{
	// Empty, place for derived classes to implement without having to hook all the other events
	// PlayerState改变时会广播事件，对于子类，可以重载此函数来避免监听对应的事件
}

void AAscePlayerController::BroadcastOnPlayerStateChanged()
{
	OnPlayerStateChanged();
	
	//LastSeenPlayerState总是上一次的PlayerState
	
	// Unbind from the old player state, if any
	// 先把原来playerstate上绑定的队伍相关的委托卸载了
	FGenericTeamId OldTeamID = FGenericTeamId::NoTeam;
	if (LastSeenPlayerState != nullptr)
	{
		if (IAsceTeamAgentInterface* PlayerStateTeamInterface = Cast<IAsceTeamAgentInterface>(LastSeenPlayerState))
		{
			OldTeamID = PlayerStateTeamInterface->GetGenericTeamId();
			PlayerStateTeamInterface->GetTeamChangedDelegateChecked().RemoveAll(this);
		}
	}

	// Bind to the new player state, if any
	FGenericTeamId NewTeamID = FGenericTeamId::NoTeam;
	if (PlayerState != nullptr)
	{
		if (IAsceTeamAgentInterface* PlayerStateTeamInterface = Cast<IAsceTeamAgentInterface>(PlayerState))
		{
			NewTeamID = PlayerStateTeamInterface->GetGenericTeamId();
			PlayerStateTeamInterface->GetTeamChangedDelegateChecked().AddDynamic(this, &ThisClass::OnPlayerStateChangedTeam);
		}
	}

	// Broadcast the team change (if it really has)
	ConditionalBroadcastTeamChanged(this, OldTeamID, NewTeamID);

	LastSeenPlayerState = PlayerState;
}

void AAscePlayerController::InitPlayerState()
{
	Super::InitPlayerState();
	BroadcastOnPlayerStateChanged();
}

void AAscePlayerController::CleanupPlayerState()
{
	Super::CleanupPlayerState();
	BroadcastOnPlayerStateChanged();
}

void AAscePlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	BroadcastOnPlayerStateChanged();

	// When we're a client connected to a remote server, the player controller may replicate later than the PlayerState and AbilitySystemComponent.
	// However, TryActivateAbilitiesOnSpawn depends on the player controller being replicated in order to check whether on-spawn abilities should
	// execute locally. Therefore once the PlayerController exists and has resolved the PlayerState, try once again to activate on-spawn abilities.
	// On other net modes the PlayerController will never replicate late, so AsceASC's own TryActivateAbilitiesOnSpawn calls will succeed. The handling 
	// here is only for when the PlayerState and ASC replicated before the PC and incorrectly thought the abilities were not for the local player.
	// 当我们作为客户端连接到远程服务器上时，playercontroller可能比PlayerState和AbilitySystemComponent复制的更晚
	// 然而，TryActivateAbilitiesOnSpawn需要PlayerController已被复制到客户端来检查生成时激活的能力是否应该本地执行。因此一旦PlayerController存在并且PlayerState也存在，
	// 就再次尝试激活生成时激活能力。在其他网络模式下PlayerController从不会更晚的复制，因此AsceASC的TryActivateAbilitiesOnSpawn将能成功执行。此处只是为了解决PlayerState
	// 和ASC在PC之前复制，并且错误的认为这些能力不属于本地的玩家。
	// 
	// 网络复制会保证此函数调用的时候必然是服务器上的Controller拥有的PlayerState对象指针发生了变化，即PlayerState改变，而PlayerState内部的属性同步走的是PlayerState的网络复制
	if (GetWorld()->IsNetMode(NM_Client))
	{
		if (AAscePlayerState* AscePS = GetPlayerState<AAscePlayerState>())
		{
			if (UAsceAbilitySystemComponent* AsceASC = AscePS->GetAsceAbilitySystemComponent())
			{
				AsceASC->RefreshAbilityActorInfo();
				AsceASC->TryActivateAbilitiesOnSpawn();
			}
		}
	}
}

void AAscePlayerController::SetPlayer(UPlayer* InPlayer)
{
	Super::SetPlayer(InPlayer);
	//当本地玩家被分配此Controller时调用，此时获取用户设置并应用
	
	// if (const UAsceLocalPlayer* AsceLocalPlayer = Cast<UAsceLocalPlayer>(InPlayer))
	// {
	// 	UAsceSettingsShared* UserSettings = AsceLocalPlayer->GetSharedSettings();
	// 	UserSettings->OnSettingChanged.AddUObject(this, &ThisClass::OnSettingsChanged);
	//
	// 	OnSettingsChanged(UserSettings);
	// }
}

void AAscePlayerController::OnSettingsChanged(UAsceSettingsShared* InSettings)
{
	bForceFeedbackEnabled = false;//InSettings->GetForceFeedbackEnabled();
}

void AAscePlayerController::AddCheats(bool bForce)
{
#if USING_CHEAT_MANAGER
	Super::AddCheats(true);
#else //#if USING_CHEAT_MANAGER
	Super::AddCheats(bForce);
#endif // #else //#if USING_CHEAT_MANAGER
}

void AAscePlayerController::ServerCheat_Implementation(const FString& Msg)
{
	//
#if USING_CHEAT_MANAGER
	if (CheatManager)
	{
		UE_LOG(LogAsce, Warning, TEXT("ServerCheat: %s"), *Msg);
		ClientMessage(ConsoleCommand(Msg));
	}
#endif // #if USING_CHEAT_MANAGER
}

bool AAscePlayerController::ServerCheat_Validate(const FString& Msg)
{
	return true;
}

void AAscePlayerController::ServerCheatAll_Implementation(const FString& Msg)
{
	//在服务器上对所有玩家作用作弊命令
#if USING_CHEAT_MANAGER
	if (CheatManager)
	{
		UE_LOG(LogAsce, Warning, TEXT("ServerCheatAll: %s"), *Msg);
		for (TActorIterator<AAscePlayerController> It(GetWorld()); It; ++It)
		{
			AAscePlayerController* AscePC = (*It);
			if (AscePC)
			{
				AscePC->ClientMessage(AscePC->ConsoleCommand(Msg));
			}
		}
	}
#endif // #if USING_CHEAT_MANAGER
}

bool AAscePlayerController::ServerCheatAll_Validate(const FString& Msg)
{
	return true;
}

void AAscePlayerController::PreProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PreProcessInput(DeltaTime, bGamePaused);
}

void AAscePlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	if (UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponent())
	{
		AsceASC->ProcessAbilityInput(DeltaTime, bGamePaused);
	}

	Super::PostProcessInput(DeltaTime, bGamePaused);
}

// void AAscePlayerController::OnCameraPenetratingTarget()
// {
// 	bHideViewTargetPawnNextFrame = true;
// }

void AAscePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

#if WITH_SERVER_CODE && WITH_EDITOR
	if (GIsEditor && (InPawn != nullptr) && (GetPawn() == InPawn))
	{
		for (const FAsceCheatToRun& CheatRow : GetDefault<UAsceDeveloperSettings>()->CheatsToRun)
		{
			if (CheatRow.Phase == ECheatExecutionTime::OnPlayerPawnPossession)
			{
				ConsoleCommand(CheatRow.Cheat, /*bWriteToLog=*/ true);
			}
		}
	}
#endif

	SetIsAutoRunning(false);
}

void AAscePlayerController::SetIsAutoRunning(const bool bEnabled)
{
	const bool bIsAutoRunning = GetIsAutoRunning();
	if (bEnabled != bIsAutoRunning)
	{
		if (!bEnabled)
		{
			OnEndAutoRun();
		}
		else
		{
			OnStartAutoRun();
		}
	}
}

bool AAscePlayerController::GetIsAutoRunning() const
{
	bool bIsAutoRunning = false;
	if (const UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponent())
	{
		bIsAutoRunning = AsceASC->GetTagCount(AsceGameplayTags::Status_AutoRunning) > 0;
	}
	return bIsAutoRunning;
}

void AAscePlayerController::OnStartAutoRun()
{
	if (UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponent())
	{
		AsceASC->SetLooseGameplayTagCount(AsceGameplayTags::Status_AutoRunning, 1);
		K2_OnStartAutoRun();
	}	
}

void AAscePlayerController::OnEndAutoRun()
{
	if (UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponent())
	{
		AsceASC->SetLooseGameplayTagCount(AsceGameplayTags::Status_AutoRunning, 0);
		K2_OnEndAutoRun();
	}
}

void AAscePlayerController::UpdateForceFeedback(IInputInterface* InputInterface, const int32 ControllerId)
{
	if (bForceFeedbackEnabled)
	{
		if (const UCommonInputSubsystem* CommonInputSubsystem = UCommonInputSubsystem::Get(GetLocalPlayer()))
		{
			const ECommonInputType CurrentInputType = CommonInputSubsystem->GetCurrentInputType();
			if (Asce::Input::ShouldAlwaysPlayForceFeedback || CurrentInputType == ECommonInputType::Gamepad || CurrentInputType == ECommonInputType::Touch)
			{
				InputInterface->SetForceFeedbackChannelValues(ControllerId, ForceFeedbackValues);
				return;
			}
		}
	}
	
	InputInterface->SetForceFeedbackChannelValues(ControllerId, FForceFeedbackValues());
}

void AAscePlayerController::UpdateHiddenComponents(const FVector& ViewLocation, TSet<FPrimitiveComponentId>& OutHiddenComponents)
{
	Super::UpdateHiddenComponents(ViewLocation, OutHiddenComponents);

	//用于相机穿透时隐藏玩家
	if (bHideViewTargetPawnNextFrame)
	{
		AActor* const ViewTargetPawn = PlayerCameraManager ? Cast<AActor>(PlayerCameraManager->GetViewTarget()) : nullptr;
		if (ViewTargetPawn)
		{
			// internal helper func to hide all the components
			auto AddToHiddenComponents = [&OutHiddenComponents](const TInlineComponentArray<UPrimitiveComponent*>& InComponents)
			{
				// add every component and all attached children
				for (UPrimitiveComponent* Comp : InComponents)
				{
					if (Comp->IsRegistered())
					{
						OutHiddenComponents.Add(Comp->GetPrimitiveSceneId());

						for (USceneComponent* AttachedChild : Comp->GetAttachChildren())
						{
							static FName NAME_NoParentAutoHide(TEXT("NoParentAutoHide"));
							UPrimitiveComponent* AttachChildPC = Cast<UPrimitiveComponent>(AttachedChild);
							if (AttachChildPC && AttachChildPC->IsRegistered() && !AttachChildPC->ComponentTags.Contains(NAME_NoParentAutoHide))
							{
								OutHiddenComponents.Add(AttachChildPC->GetPrimitiveSceneId());
							}
						}
					}
				}
			};

			//TODO Solve with an interface.  Gather hidden components or something.
			//TODO Hiding isn't awesome, sometimes you want the effect of a fade out over a proximity, needs to bubble up to designers.

			// hide pawn's components
			TInlineComponentArray<UPrimitiveComponent*> PawnComponents;
			ViewTargetPawn->GetComponents(PawnComponents);
			AddToHiddenComponents(PawnComponents);

			//// hide weapon too
			//if (ViewTargetPawn->CurrentWeapon)
			//{
			//	TInlineComponentArray<UPrimitiveComponent*> WeaponComponents;
			//	ViewTargetPawn->CurrentWeapon->GetComponents(WeaponComponents);
			//	AddToHiddenComponents(WeaponComponents);
			//}
		}

		// we consumed it, reset for next frame
		bHideViewTargetPawnNextFrame = false;
	}
}

void AAscePlayerController::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	//重载了队伍接口, 用来获取该Controller属于的队伍（实际上从PlayerState获取），以及为Controller提供返回队伍改变委托的接口，设置队伍Id永远不应该发生在Controller身上，因此此函数调到了就直接报错
	UE_LOG(LogAsceTeams, Error, TEXT("You can't set the team ID on a player controller (%s); it's driven by the associated player state"), *GetPathNameSafe(this));
}

FGenericTeamId AAscePlayerController::GetGenericTeamId() const
{
	if (const IAsceTeamAgentInterface* PSWithTeamInterface = Cast<IAsceTeamAgentInterface>(PlayerState))
	{
		return PSWithTeamInterface->GetGenericTeamId();
	}
	return FGenericTeamId::NoTeam;
}

FOnAsceTeamIndexChangedDelegate* AAscePlayerController::GetOnTeamIndexChangedDelegate()
{
	return &OnTeamChangedDelegate;
}

void AAscePlayerController::OnUnPossess()
{
	// Make sure the pawn that is being unpossessed doesn't remain our ASC's avatar actor
	// 确保将要被UnPossess的Pawn不再被ASC作为Avatar
	if (APawn* PawnBeingUnpossessed = GetPawn())
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerState))
		{
			if (ASC->GetAvatarActor() == PawnBeingUnpossessed)
			{
				ASC->SetAvatarActor(nullptr);
			}
		}
	}

	Super::OnUnPossess();
}

//////////////////////////////////////////////////////////////////////
// ALyraReplayPlayerController
// 还没看这个

void AAsceReplayPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The state may go invalid at any time due to scrubbing during a replay
	if (!IsValid(FollowedPlayerState))
	{
		UWorld* World = GetWorld();

		// Listen for changes for both recording and playback
		if (AAsceGameState* GameState = Cast<AAsceGameState>(World->GetGameState()))
		{
			if (!GameState->OnRecorderPlayerStateChangedEvent.IsBoundToObject(this))
			{
				GameState->OnRecorderPlayerStateChangedEvent.AddUObject(this, &ThisClass::RecorderPlayerStateUpdated);
			}
			if (APlayerState* RecorderState = GameState->GetRecorderPlayerState())
			{
				RecorderPlayerStateUpdated(RecorderState);
			}
		}
	}
}

void AAsceReplayPlayerController::SmoothTargetViewRotation(APawn* TargetPawn, float DeltaSeconds)
{
	// Default behavior is to interpolate to TargetViewRotation which is set from APlayerController::TickActor but it's not very smooth

	Super::SmoothTargetViewRotation(TargetPawn, DeltaSeconds);
}

bool AAsceReplayPlayerController::ShouldRecordClientReplay()
{
	return false;
}

void AAsceReplayPlayerController::RecorderPlayerStateUpdated(APlayerState* NewRecorderPlayerState)
{
	if (NewRecorderPlayerState)
	{
		FollowedPlayerState = NewRecorderPlayerState;

		// Bind to when pawn changes and call now
		NewRecorderPlayerState->OnPawnSet.AddUniqueDynamic(this, &AAsceReplayPlayerController::OnPlayerStatePawnSet);
		OnPlayerStatePawnSet(NewRecorderPlayerState, NewRecorderPlayerState->GetPawn(), nullptr);
	}
}

void AAsceReplayPlayerController::OnPlayerStatePawnSet(APlayerState* ChangedPlayerState, APawn* NewPlayerPawn, APawn* OldPlayerPawn)
{
	if (ChangedPlayerState == FollowedPlayerState)
	{
		SetViewTarget(NewPlayerPawn);
	}
}
