// Fill out your copyright notice in the Description page of Project Settings.


#include "AsceUserFacingExperienceDefinition.h"
#include "CommonSessionSubsystem.h"
#include "Containers/UnrealString.h"
#include "UObject/NameTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
//#include "Replays/AsceReplaySubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceUserFacingExperienceDefinition)

UCommonSession_HostSessionRequest* UAsceUserFacingExperienceDefinition::CreateHostingRequest(const UObject* WorldContextObject) const
{
	//CommonUser插件提供的启动游戏会话的功能
	//此函数用于创建一个包含了这个模式选择设置的会话请求对象
	//这个对象作为主机存在（host），用来接收对应参数的会话请求
	const FString ExperienceName = ExperienceID.PrimaryAssetName.ToString();
	const FString UserFacingExperienceName = GetPrimaryAssetId().PrimaryAssetName.ToString();

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UCommonSession_HostSessionRequest* Result = nullptr;
	
	if (UCommonSessionSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UCommonSessionSubsystem>() : nullptr)
	{
		Result = Subsystem->CreateOnlineHostSessionRequest();
	}

	if (!Result)
	{
		// Couldn't use the subsystem so create one
		Result = NewObject<UCommonSession_HostSessionRequest>();
		Result->OnlineMode = ECommonSessionOnlineMode::Online;
		Result->bUseLobbies = true;
		Result->bUseLobbiesVoiceChat = false;
		// We always enable presence on this session because it is the primary session used for matchmaking. For online systems that care about presence, only the primary session should have presence enabled
		Result->bUsePresence = !IsRunningDedicatedServer();
	}
	Result->MapID = MapID;
	Result->ModeNameForAdvertisement = UserFacingExperienceName;
	Result->ExtraArgs = ExtraArgs;
	Result->ExtraArgs.Add(TEXT("Experience"), ExperienceName);
	Result->MaxPlayerCount = MaxPlayerCount;

	
	//暂时警用回放
	//if (UAsceReplaySubsystem::DoesPlatformSupportReplays())
	//{
	//	if (bRecordReplay)
	//	{
	//		Result->ExtraArgs.Add(TEXT("DemoRec"), FString());
	//	}
	//}

	return Result;
}

