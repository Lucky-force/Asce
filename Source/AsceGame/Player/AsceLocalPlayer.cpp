// Fill out your copyright notice in the Description page of Project Settings.

#include "Player/AsceLocalPlayer.h"

#include "AudioMixerBlueprintLibrary.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Settings/AsceSettingsLocal.h"
#include "Settings/AsceSettingsShared.h"
#include "CommonUserSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceLocalPlayer)

class UObject;

UAsceLocalPlayer::UAsceLocalPlayer()
{
}

void UAsceLocalPlayer::PostInitProperties()
{
	Super::PostInitProperties();

	//在C++构造函数之后调用，调用super后，此时基本属性已经读取完成（包括从配置文件中读取的数据）
	//这里也是从配置文件等地方读取并初始化object数据的最佳位置, 此处为本地设置对象绑定一个音频输出设备切换时回调的委托用于修改LocalPlayer的相关设置
	
	if (UAsceSettingsLocal* LocalSettings = GetLocalSettings())
	{
		//LocalSettings->OnAudioOutputDeviceChanged.AddUObject(this, &UAsceLocalPlayer::OnAudioOutputDeviceChanged);
	}
}

void UAsceLocalPlayer::SwitchController(class APlayerController* PC)
{
	Super::SwitchController(PC);
	
	//当动态为玩家修改Controller时会调用此函数，在此处发一个controller改变的事件
	
	OnPlayerControllerChanged(PlayerController);
}

bool UAsceLocalPlayer::SpawnPlayActor(const FString& URL, FString& OutError, UWorld* InWorld)
{
	const bool bResult = Super::SpawnPlayActor(URL, OutError, InWorld);
	//游戏加载流程中调用此函数来生成controller, 此处同样发一下controller改变的事件
	
	OnPlayerControllerChanged(PlayerController);

	return bResult;
}

void UAsceLocalPlayer::InitOnlineSession()
{
	OnPlayerControllerChanged(PlayerController);

	Super::InitOnlineSession();
}

void UAsceLocalPlayer::OnPlayerControllerChanged(APlayerController* NewController)
{
	// Stop listening for changes from the old controller
	// Controller改变时，先移除LocalPlayer绑定到旧的Controller身上的委托，随后绑定到新Controller身上
	FGenericTeamId OldTeamID = FGenericTeamId::NoTeam;
	if (IAsceTeamAgentInterface* ControllerAsTeamProvider = Cast<IAsceTeamAgentInterface>(LastBoundPC.Get()))
	{
		OldTeamID = ControllerAsTeamProvider->GetGenericTeamId();
		ControllerAsTeamProvider->GetTeamChangedDelegateChecked().RemoveAll(this);
	}

	// Grab the current team ID and listen for future changes
	FGenericTeamId NewTeamID = FGenericTeamId::NoTeam;
	if (IAsceTeamAgentInterface* ControllerAsTeamProvider = Cast<IAsceTeamAgentInterface>(NewController))
	{
		NewTeamID = ControllerAsTeamProvider->GetGenericTeamId();
		ControllerAsTeamProvider->GetTeamChangedDelegateChecked().AddDynamic(this, &ThisClass::OnControllerChangedTeam);
		LastBoundPC = NewController;
	}

	ConditionalBroadcastTeamChanged(this, OldTeamID, NewTeamID);
}

void UAsceLocalPlayer::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	// Do nothing, we merely observe the team of our associated player controller
}

FGenericTeamId UAsceLocalPlayer::GetGenericTeamId() const
{
	if (IAsceTeamAgentInterface* ControllerAsTeamProvider = Cast<IAsceTeamAgentInterface>(PlayerController))
	{
		return ControllerAsTeamProvider->GetGenericTeamId();
	}
	else
	{
		return FGenericTeamId::NoTeam;
	}
}

FOnAsceTeamIndexChangedDelegate* UAsceLocalPlayer::GetOnTeamIndexChangedDelegate()
{
	return &OnTeamChangedDelegate;
}

UAsceSettingsLocal* UAsceLocalPlayer::GetLocalSettings() const
{
	return UAsceSettingsLocal::Get();
}

UAsceSettingsShared* UAsceLocalPlayer::GetSharedSettings() const
{
	if (!SharedSettings)
	{
		// On PC it's okay to use the sync load because it only checks the disk
		// This could use a platform tag to check for proper save support instead
		bool bCanLoadBeforeLogin = PLATFORM_DESKTOP;
		
		if (bCanLoadBeforeLogin)
		{
			//SharedSettings = UAsceSettingsShared::LoadOrCreateSettings(this);
		}
		else
		{
			// We need to wait for user login to get the real settings so return temp ones
			//SharedSettings = UAsceSettingsShared::CreateTemporarySettings(this);
		}
	}

	return SharedSettings;
}

void UAsceLocalPlayer::LoadSharedSettingsFromDisk(bool bForceLoad)
{
	FUniqueNetIdRepl CurrentNetId = GetCachedUniqueNetId();
	if (!bForceLoad && SharedSettings && CurrentNetId == NetIdForSharedSettings)
	{
		// Already loaded once, don't reload
		return;
	}

	//ensure(UAsceSettingsShared::AsyncLoadOrCreateSettings(this, UAsceSettingsShared::FOnSettingsLoadedEvent::CreateUObject(this, &UAsceLocalPlayer::OnSharedSettingsLoaded)));
}

void UAsceLocalPlayer::OnSharedSettingsLoaded(UAsceSettingsShared* LoadedOrCreatedSettings)
{
	// The settings are applied before it gets here
	if (ensure(LoadedOrCreatedSettings))
	{
		// This will replace the temporary or previously loaded object which will GC out normally
		SharedSettings = LoadedOrCreatedSettings;

		NetIdForSharedSettings = GetCachedUniqueNetId();
	}
}

void UAsceLocalPlayer::OnAudioOutputDeviceChanged(const FString& InAudioOutputDeviceId)
{
	FOnCompletedDeviceSwap DevicesSwappedCallback;
	DevicesSwappedCallback.BindUFunction(this, FName("OnCompletedAudioDeviceSwap"));
	UAudioMixerBlueprintLibrary::SwapAudioOutputDevice(GetWorld(), InAudioOutputDeviceId, DevicesSwappedCallback);
}

void UAsceLocalPlayer::OnCompletedAudioDeviceSwap(const FSwapAudioOutputResult& SwapResult)
{
	if (SwapResult.Result == ESwapAudioOutputDeviceResultState::Failure)
	{
	}
}

void UAsceLocalPlayer::OnControllerChangedTeam(UObject* TeamAgent, int32 OldTeam, int32 NewTeam)
{
	ConditionalBroadcastTeamChanged(this, IntegerToGenericTeamId(OldTeam), IntegerToGenericTeamId(NewTeam));
}

