// Fill out your copyright notice in the Description page of Project Settings.


// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsceGameMode.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "AsceLogChannels.h"
#include "Misc/CommandLine.h"
#include "System/AsceAssetManager.h"
#include "AsceGameState.h"
#include "System/AsceGameSession.h"
#include "Player/AscePlayerController.h"
//#include "Player/AscePlayerBotController.h"
#include "Player/AScePlayerState.h"
#include "Character/AsceCharacter.h"
#include "UI/AsceHUD.h"
#include "Character/AscePawnExtensionComponent.h"
#include "Character/AscePawnData.h"
#include "GameModes/AsceWorldSettings.h"
#include "GameModes/AsceExperienceDefinition.h"
#include "GameModes/AsceExperienceManagerComponent.h"
#include "GameModes/AsceUserFacingExperienceDefinition.h"
#include "Kismet/GameplayStatics.h"
#include "Development/AsceDeveloperSettings.h"
#include "Player/AscePlayerSpawningManagerComponent.h"
#include "CommonUserSubsystem.h"
#include "CommonSessionSubsystem.h"
#include "TimerManager.h"
#include "GameMapsSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceGameMode)

AAsceGameMode::AAsceGameMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	//构造函数预先使用的默认值
	GameStateClass = AAsceGameState::StaticClass();
	GameSessionClass = AAsceGameSession::StaticClass();
	PlayerControllerClass = AAscePlayerController::StaticClass();
	ReplaySpectatorPlayerControllerClass = AAsceReplayPlayerController::StaticClass();
	PlayerStateClass = AAscePlayerState::StaticClass();
	DefaultPawnClass = AAsceCharacter::StaticClass();
	HUDClass = AAsceHUD::StaticClass();
}

const UAscePawnData* AAsceGameMode::GetPawnDataForController(const AController* InController) const
{
	// See if pawn data is already set on the player state
	// 先检查PlayerState是否已经有PanwData了, 有了就直接返回这个PanwData
	if (InController != nullptr)
	{
		if (const AAscePlayerState* AscePS = InController->GetPlayerState<AAscePlayerState>())
		{
			if (const UAscePawnData* PawnData = AscePS->GetPawnData<UAscePawnData>())
			{
				return PawnData;
			}
		}
	}

	// If not, fall back to the the default for the current experience
	// PlayerState上面没有找到PawnData，从当前的Exprience中获取
	check(GameState);
	UAsceExperienceManagerComponent* ExperienceComponent = GameState->FindComponentByClass<UAsceExperienceManagerComponent>();
	check(ExperienceComponent);

	if (ExperienceComponent->IsExperienceLoaded())
	{
		const UAsceExperienceDefinition* Experience = ExperienceComponent->GetCurrentExperienceChecked();
		if (Experience->DefaultPawnData != nullptr)
		{
			return Experience->DefaultPawnData;
		}

		// Experience is loaded and there's still no pawn data, fall back to the default for now
		// Experience已加载，但是上面没有配置PanwData，使用默认值
		return UAsceAssetManager::Get().GetDefaultPawnData();
	}

	// Experience not loaded yet, so there is no pawn data to be had
	// 此时Experience还没有加载完成，先返回Nullptr
	return nullptr;
}

void AAsceGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// Wait for the next frame to give time to initialize startup settings
	// 下一帧正式开始初始化（等待引擎的原有流程完成）
	GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::HandleMatchAssignmentIfNotExpectingOne);
}

void AAsceGameMode::HandleMatchAssignmentIfNotExpectingOne()
{
	FPrimaryAssetId ExperienceId;
	FString ExperienceIdSource;

	// Precedence order (highest wins)
	//  - Matchmaking assignment (if present)
	//  - URL Options override
	//  - Developer Settings (PIE only)
	//  - Command Line override
	//  - World Settings
	//  - Dedicated server
	//  - Default experience
	
	// Experience的获取顺序
	// 1.本次比赛直接赋予
	// 2.URL的Options重载
	// 3.PIE下的开发者设置
	// 4.命令行重载
	// 5.WorldSettings重载
	// 6.DS
	// 7.默认值

	UWorld* World = GetWorld();

	//URL的Option重载Experience
	if (!ExperienceId.IsValid() && UGameplayStatics::HasOption(OptionsString, TEXT("Experience")))
	{
		const FString ExperienceFromOptions = UGameplayStatics::ParseOption(OptionsString, TEXT("Experience"));
		ExperienceId = FPrimaryAssetId(FPrimaryAssetType(UAsceExperienceDefinition::StaticClass()->GetFName()), FName(*ExperienceFromOptions));
		ExperienceIdSource = TEXT("OptionsString");
	}

	//URL没找到，从开发者设置中获取
	if (!ExperienceId.IsValid() && World->IsPlayInEditor())
	{
		ExperienceId = GetDefault<UAsceDeveloperSettings>()->ExperienceOverride;
		ExperienceIdSource = TEXT("DeveloperSettings");
	}

	// see if the command line wants to set the experience
	// 还是没有找到，尝试从CommandLine中获取
	if (!ExperienceId.IsValid())
	{
		FString ExperienceFromCommandLine;
		if (FParse::Value(FCommandLine::Get(), TEXT("Experience="), ExperienceFromCommandLine))
		{
			ExperienceId = FPrimaryAssetId::ParseTypeAndName(ExperienceFromCommandLine);
			if (!ExperienceId.PrimaryAssetType.IsValid())
			{
				ExperienceId = FPrimaryAssetId(FPrimaryAssetType(UAsceExperienceDefinition::StaticClass()->GetFName()), FName(*ExperienceFromCommandLine));
			}
			ExperienceIdSource = TEXT("CommandLine");
		}
	}

	// see if the world settings has a default experience
	// 尝试从WorldSettings中获取
	if (!ExperienceId.IsValid())
	{
		if (AAsceWorldSettings* TypedWorldSettings = Cast<AAsceWorldSettings>(GetWorldSettings()))
		{
			ExperienceId = TypedWorldSettings->GetDefaultGameplayExperience();
			ExperienceIdSource = TEXT("WorldSettings");
		}
	}

	// 尝试获取一下上面获取到的ExperienceId对应的AssetData, 这里获取失败的话直接充值ExperienceId
	UAsceAssetManager& AssetManager = UAsceAssetManager::Get();
	FAssetData Dummy;
	if (ExperienceId.IsValid() && !AssetManager.GetPrimaryAssetData(ExperienceId, /*out*/ Dummy))
	{
		UE_LOG(LogAsceExperience, Error, TEXT("EXPERIENCE: Wanted to use %s but couldn't find it, falling back to the default)"), *ExperienceId.ToString());
		ExperienceId = FPrimaryAssetId();
	}

	// Final fallback to the default experience
	// 最终各种途径都失败了，使用默认的Experience吧
	if (!ExperienceId.IsValid())
	{
		//ExperienceId非法，先尝试以DS的方式启动
		//这里总是期望不会走到，仅仅作为扩展功能，理想的联机方法是一个客户端作为listenserver直接联机，而不是使用ds
		if (TryDedicatedServerLogin())
		{
			// This will start to host as a dedicated server
			return;
		}

		//@TODO: Pull this from a config setting or something
		ExperienceId = FPrimaryAssetId(FPrimaryAssetType("AsceExperienceDefinition"), FName("B_AsceDefaultExperience"));
		ExperienceIdSource = TEXT("Default");
	}
	
	OnMatchAssignmentGiven(ExperienceId, ExperienceIdSource);
}

bool AAsceGameMode::TryDedicatedServerLogin()
{
	// 这里是在运行在线子系统，我并不希望在线子系统在这个项目中使用，在线子系统提供了匹配，排行榜，好友等功能
	// 并没有什么卵用，并且这个系统全是坑，用起来很难受，同时这不是一款需要匹配的大型游戏，这只是一个可以朋友间
	// 进行联机合作的小游戏，使用在线子系统太过于笨重了，使用ue自带的DS连接就好
	
	
	// Some basic code to register as an active dedicated server, this would be heavily modified by the game
	// 看看是不是在DS上，然后尝试注册DS的激活状态
	FString DefaultMap = UGameMapsSettings::GetGameDefaultMap();
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance && World && World->GetNetMode() == NM_DedicatedServer && World->URL.Map == DefaultMap)
	{
		//这里删除了对在线子系统的使用，直接开启DS的启动流程
		
		// Only register if this is the default map on a dedicated server
		// 仅在当前进程的世界的网络模式是DS并且当前世界的地图是默认地图的时候注册到CommonUserSubsystem中
		//UCommonUserSubsystem* UserSubsystem = GameInstance->GetSubsystem<UCommonUserSubsystem>();

		// Dedicated servers may need to do an online login
		//UserSubsystem->OnUserInitializeComplete.AddDynamic(this, &AAsceGameMode::OnUserInitializedForDedicatedServer);

		// There are no local users on dedicated server, but index 0 means the default platform user which is handled by the online login code
		//if (!UserSubsystem->TryToLoginForOnlinePlay(0))
		//{
		OnUserInitializedForDedicatedServer(nullptr, false, FText(), ECommonUserPrivilege::CanPlayOnline, ECommonUserOnlineContext::Default);
		//}
		return true;
	}

	return false;
}

void AAsceGameMode::HostDedicatedServerMatch(ECommonSessionOnlineMode OnlineMode)
{
	//这里是正式启动DS的地方，如果进入这一步，Experience的加载将在这里进行了，HandleMatchAssignmentIfNotExpectingOne的流程在确认为DS后就不再继续
	
	//默认DS启动的时候是在大厅地图，此时用UserFacingExperience来显示房间UI信息
	FPrimaryAssetType UserExperienceType = UAsceUserFacingExperienceDefinition::StaticClass()->GetFName();
	
	// 找一下从何处加载UserFacingExperience
	// Figure out what UserFacingExperience to load
	FPrimaryAssetId UserExperienceId;
	
	//先从命令行解析出UserFacingExperience的PrimaryAssetId
	FString UserExperienceFromCommandLine;
	if (FParse::Value(FCommandLine::Get(), TEXT("UserExperience="), UserExperienceFromCommandLine) ||
		FParse::Value(FCommandLine::Get(), TEXT("Playlist="), UserExperienceFromCommandLine))
	{
		UserExperienceId = FPrimaryAssetId::ParseTypeAndName(UserExperienceFromCommandLine);
		if (!UserExperienceId.PrimaryAssetType.IsValid())
		{
			UserExperienceId = FPrimaryAssetId(FPrimaryAssetType(UserExperienceType), FName(*UserExperienceFromCommandLine));
		}
	}

	// Search for the matching experience, it's fine to force load them because we're in dedicated server startup
	// 直接把所有的UserFacingExperience加载出来然后匹配，因为现在是在DS的启动阶段，所以直接强制加载也可以的
	UAsceAssetManager& AssetManager = UAsceAssetManager::Get();
	TSharedPtr<FStreamableHandle> Handle = AssetManager.LoadPrimaryAssetsWithType(UserExperienceType);
	if (ensure(Handle.IsValid()))
	{
		//同步等待(UE的所有加载都是异步的，同步加载仅仅是在异步句柄返回后强制阻塞到加载完成)
		Handle->WaitUntilComplete();
	}

	TArray<UObject*> UserExperiences;
	AssetManager.GetPrimaryAssetObjectList(UserExperienceType, UserExperiences);
	UAsceUserFacingExperienceDefinition* FoundExperience = nullptr;
	UAsceUserFacingExperienceDefinition* DefaultExperience = nullptr;

	//遍历看看哪个的ID和命令行传入的ID一致
	for (UObject* Object : UserExperiences)
	{
		UAsceUserFacingExperienceDefinition* UserExperience = Cast<UAsceUserFacingExperienceDefinition>(Object);
		if (ensure(UserExperience))
		{
			if (UserExperience->GetPrimaryAssetId() == UserExperienceId)
			{
				FoundExperience = UserExperience;
				break;
			}
			
			if (UserExperience->bIsDefaultExperience && DefaultExperience == nullptr)
			{
				DefaultExperience = UserExperience;
			}
		}
	}

	// 没找到对应的就用DefaultExperience
	if (FoundExperience == nullptr)
	{
		FoundExperience = DefaultExperience;
	}
	
	UGameInstance* GameInstance = GetGameInstance();
	if (ensure(FoundExperience && GameInstance))
	{
		// Actually host the game
		UCommonSession_HostSessionRequest* HostRequest = FoundExperience->CreateHostingRequest(this);
		if (ensure(HostRequest))
		{
			HostRequest->OnlineMode = OnlineMode;

			// TODO override other parameters?

			UCommonSessionSubsystem* SessionSubsystem = GameInstance->GetSubsystem<UCommonSessionSubsystem>();
			SessionSubsystem->HostSession(nullptr, HostRequest);
			
			// This will handle the map travel
		}
	}

}

void AAsceGameMode::OnUserInitializedForDedicatedServer(const UCommonUserInfo* UserInfo, bool bSuccess, FText Error, ECommonUserPrivilege RequestedPrivilege, ECommonUserOnlineContext OnlineContext)
{
	//DS的启动流程，或许应该改个名字，现在不使用在线子系统了，因此传入的UserInfo必定为nullptr, bSuccess总是为false
	//直接开始真正的DS启动流程
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance)
	{
		// Unbind
		//UCommonUserSubsystem* UserSubsystem = GameInstance->GetSubsystem<UCommonUserSubsystem>();
		//UserSubsystem->OnUserInitializeComplete.RemoveDynamic(this, &AAsceGameMode::OnUserInitializedForDedicatedServer);

		// Dedicated servers do not require user login, but some online subsystems may expect it
		//if (bSuccess && ensure(UserInfo))
		//{
		//	UE_LOG(LogAsceExperience, Log, TEXT("Dedicated server user login succeeded for id %s, starting online server"), *UserInfo->GetNetId().ToString());
		//}
		//else
		//{
		//	UE_LOG(LogAsceExperience, Log, TEXT("Dedicated server user login unsuccessful, starting online server as login is not required"));
		//}
		
		HostDedicatedServerMatch(ECommonSessionOnlineMode::Online);
	}
}

void AAsceGameMode::OnMatchAssignmentGiven(FPrimaryAssetId ExperienceId, const FString& ExperienceIdSource)
{
	if (ExperienceId.IsValid())
	{
		UE_LOG(LogAsceExperience, Log, TEXT("Identified experience %s (Source: %s)"), *ExperienceId.ToString(), *ExperienceIdSource);

		UAsceExperienceManagerComponent* ExperienceComponent = GameState->FindComponentByClass<UAsceExperienceManagerComponent>();
		check(ExperienceComponent);
		ExperienceComponent->SetCurrentExperience(ExperienceId);
	}
	else
	{
		UE_LOG(LogAsceExperience, Error, TEXT("Failed to identify experience, loading screen will stay up forever"));
	}
}

void AAsceGameMode::OnExperienceLoaded(const UAsceExperienceDefinition* CurrentExperience)
{
	
	// Experience完成加载之后调用，此时GameFeature的Actions都已经全部完成了调用
	// 可以开始生产玩家的pawn了
	// Spawn any players that are already attached
	//@TODO: Here we're handling only *player* controllers, but in GetDefaultPawnClassForController_Implementation we skipped all controllers
	// GetDefaultPawnClassForController_Implementation might only be getting called for players anyways
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* PC = Cast<APlayerController>(*Iterator);
		if ((PC != nullptr) && (PC->GetPawn() == nullptr))
		{
			if (PlayerCanRestart(PC))
			{
				RestartPlayer(PC);//在FindPlayerStart返回的位置生成玩家的Pawn（并且posse）
			}
		}
	}
}

bool AAsceGameMode::IsExperienceLoaded() const
{
	check(GameState);
	UAsceExperienceManagerComponent* ExperienceComponent = GameState->FindComponentByClass<UAsceExperienceManagerComponent>();
	check(ExperienceComponent);

	return ExperienceComponent->IsExperienceLoaded();
}

UClass* AAsceGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	if (const UAscePawnData* PawnData = GetPawnDataForController(InController))
	{
		if (PawnData->PawnClass)
		{
			return PawnData->PawnClass;
		}
	}

	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

APawn* AAsceGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	//RestartPlayer最终调用SpawnDefaultPawnAtTransform来生成玩家，这里重载一下这个函数
	
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Instigator = GetInstigator();
	SpawnInfo.ObjectFlags |= RF_Transient;	// 修改flag防止生成的玩家被保存到地图当中
	SpawnInfo.bDeferConstruction = true;

	if (UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer))
	{
		if (APawn* SpawnedPawn = GetWorld()->SpawnActor<APawn>(PawnClass, SpawnTransform, SpawnInfo))
		{
			if (UAscePawnExtensionComponent* PawnExtComp = UAscePawnExtensionComponent::FindPawnExtensionComponent(SpawnedPawn))
			{
				if (const UAscePawnData* PawnData = GetPawnDataForController(NewPlayer))
				{
					PawnExtComp->SetPawnData(PawnData);
				}
				else
				{
					UE_LOG(LogAsce, Error, TEXT("Game mode was unable to set PawnData on the spawned pawn [%s]."), *GetNameSafe(SpawnedPawn));
				}
			}

			SpawnedPawn->FinishSpawning(SpawnTransform);

			return SpawnedPawn;
		}
		else
		{
			UE_LOG(LogAsce, Error, TEXT("Game mode was unable to spawn Pawn of class [%s] at [%s]."), *GetNameSafe(PawnClass), *SpawnTransform.ToHumanReadableString());
		}
	}
	else
	{
		UE_LOG(LogAsce, Error, TEXT("Game mode was unable to spawn Pawn due to NULL pawn class."));
	}

	return nullptr;
}

bool AAsceGameMode::ShouldSpawnAtStartSpot(AController* Player)
{
	// We never want to use the start spot, always use the spawn management component.
	// 从不在StartSpot生成玩家，而是使用SpawnManangementComponent来生成玩家，此处总是返回false
	return false;
}

void AAsceGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Delay starting new players until the experience has been loaded
	// (players who log in prior to that will be started by OnExperienceLoaded)
	// SeamlessTravel（玩家已经登录，但是切换地图）的时候调用此函数来生成Pawn，需要在Experience加载完成才能生成
	if (IsExperienceLoaded())
	{
		Super::HandleStartingNewPlayer_Implementation(NewPlayer);
	}
}

AActor* AAsceGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (UAscePlayerSpawningManagerComponent* PlayerSpawningComponent = GameState->FindComponentByClass<UAscePlayerSpawningManagerComponent>())
	{
		return PlayerSpawningComponent->ChoosePlayerStart(Player);
	}
	
	return Super::ChoosePlayerStart_Implementation(Player);
}

void AAsceGameMode::FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation)
{
	if (UAscePlayerSpawningManagerComponent* PlayerSpawningComponent = GameState->FindComponentByClass<UAscePlayerSpawningManagerComponent>())
	{
		PlayerSpawningComponent->FinishRestartPlayer(NewPlayer, StartRotation);
	}

	Super::FinishRestartPlayer(NewPlayer, StartRotation);
}

bool AAsceGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	return ControllerCanRestart(Player);
}

bool AAsceGameMode::ControllerCanRestart(AController* Controller)
{
	if (APlayerController* PC = Cast<APlayerController>(Controller))
	{	
		if (!Super::PlayerCanRestart_Implementation(PC))
		{
			return false;
		}
	}
	else
	{
		// Bot version of Super::PlayerCanRestart_Implementation
		if ((Controller == nullptr) || Controller->IsPendingKillPending())
		{
			return false;
		}
	}

	if (UAscePlayerSpawningManagerComponent* PlayerSpawningComponent = GameState->FindComponentByClass<UAscePlayerSpawningManagerComponent>())
	{
		return PlayerSpawningComponent->ControllerCanRestart(Controller);
	}

	return true;
}

void AAsceGameMode::InitGameState()
{
	Super::InitGameState();

	// Listen for the experience load to complete	
	UAsceExperienceManagerComponent* ExperienceComponent = GameState->FindComponentByClass<UAsceExperienceManagerComponent>();
	check(ExperienceComponent);
	ExperienceComponent->CallOrRegister_OnExperienceLoaded(FOnAsceExperienceLoaded::FDelegate::CreateUObject(this, &ThisClass::OnExperienceLoaded));
}

void AAsceGameMode::GenericPlayerInitialization(AController* NewPlayer)
{
	Super::GenericPlayerInitialization(NewPlayer);

	// 给玩家分配**跨端一致的 TeamId**（从 1 开始；超出本端阵营表容量就不分，视为中立/观战）。
	// 只在服务器执行；客户端靠 PlayerState 复制 + OnRep 得到同样的 TeamId。
	if (HasAuthority())
	{
		if (AAscePlayerState* PlayerState = NewPlayer ? NewPlayer->GetPlayerState<AAscePlayerState>() : nullptr)
		{
			if (PlayerState->GetGenericTeamId() == FGenericTeamId::NoTeam)
			{
				if (NextTeamId <= AsceMaxTeams)
				{
					const uint8 TeamId = static_cast<uint8>(NextTeamId++);
					PlayerState->SetGenericTeamId(FGenericTeamId(TeamId));

					// 服务器本端阵营表立即登记（AddPlayerState 的 seed 跑在"设队"之前，等不到下次 seed）
					if (AAsceGameState* GS = GetGameState<AAsceGameState>())
					{
						GS->GetOrAssignTeamIndex(TeamId);
					}
				}
				else
				{
					UE_LOG(LogAsceTeams, Warning,
						TEXT("[Asce] 本端阵营表已满（%d 队），玩家 %s 不分配队伍（中立）"),
						AsceMaxTeams, *GetNameSafe(PlayerState));
				}
			}
		}
	}

	OnGameModePlayerInitialized.Broadcast(this, NewPlayer);
}

void AAsceGameMode::RequestPlayerRestartNextFrame(AController* Controller, bool bForceReset)
{
	//因为各种原因restartplayer失败了，就下一帧再调用，知道restart成功
	if (bForceReset && (Controller != nullptr))
	{
		Controller->Reset();
	}

	if (APlayerController* PC = Cast<APlayerController>(Controller))
	{
		GetWorldTimerManager().SetTimerForNextTick(PC, &APlayerController::ServerRestartPlayer_Implementation);
	}
	//else if (AAscePlayerBotController* BotController = Cast<AAscePlayerBotController>(Controller))
	//{
	//	GetWorldTimerManager().SetTimerForNextTick(BotController, &AAscePlayerBotController::ServerRestartController);
	//}
}

bool AAsceGameMode::UpdatePlayerStartSpot(AController* Player, const FString& Portal, FString& OutErrorMessage)
{
	// Do nothing, we'll wait until PostLogin when we try to spawn the player for real.
	// Doing anything right now is no good, systems like team assignment haven't even occurred yet.
	return true;
}

void AAsceGameMode::FailedToRestartPlayer(AController* NewPlayer)
{
	Super::FailedToRestartPlayer(NewPlayer);

	// If we tried to spawn a pawn and it failed, lets try again *note* check if there's actually a pawn class
	// before we try this forever.
	if (UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer))
	{
		if (APlayerController* NewPC = Cast<APlayerController>(NewPlayer))
		{
			// If it's a player don't loop forever, maybe something changed and they can no longer restart if so stop trying.
			if (PlayerCanRestart(NewPC))
			{
				RequestPlayerRestartNextFrame(NewPlayer, false);			
			}
			else
			{
				UE_LOG(LogAsce, Verbose, TEXT("FailedToRestartPlayer(%s) and PlayerCanRestart returned false, so we're not going to try again."), *GetPathNameSafe(NewPlayer));
			}
		}
		else
		{
			RequestPlayerRestartNextFrame(NewPlayer, false);
		}
	}
	else
	{
		UE_LOG(LogAsce, Verbose, TEXT("FailedToRestartPlayer(%s) but there's no pawn class so giving up."), *GetPathNameSafe(NewPlayer));
	}
}
