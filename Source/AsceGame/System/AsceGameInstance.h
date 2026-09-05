// Fill out your copyright notice in the Description page of Project Settings.

#pragma once


#include "CommonGameInstance.h"

#include "AsceGameInstance.generated.h"

#define UE_API ASCEGAME_API

class AAscePlayerController;
class UObject;

UCLASS(MinimalAPI, Config = Game)
class UAsceGameInstance : public UCommonGameInstance
{
	GENERATED_BODY()

public:

	UE_API UAsceGameInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UE_API AAscePlayerController* GetPrimaryPlayerController() const;
	
	//检查是否可以加入当前请求加入的会话
	UE_API virtual bool CanJoinRequestedSession() const override;
	
	//成功登录后调用，加载玩家的本地设置
	UE_API virtual void HandlerUserInitialized(const UCommonUserInfo* UserInfo, bool bSuccess, FText Error, ECommonUserPrivilege RequestedPrivilege, ECommonUserOnlineContext OnlineContext) override;

	//在收到网络加密token的时候调用
	UE_API virtual void ReceivedNetworkEncryptionToken(const FString& EncryptionToken, const FOnEncryptionKeyResponse& Delegate) override;
	
	//在收到网络加密Ack的时候调用, 填充数据，随后执行传入的委托来响应
	UE_API virtual void ReceivedNetworkEncryptionAck(const FOnEncryptionKeyResponse& Delegate) override;

protected:

	UE_API virtual void Init() override;
	UE_API virtual void Shutdown() override;

	//客户端travel之前调用，此处检查是否开启了密钥测试，如果是，则在url中附带相关信息
	UE_API void OnPreClientTravelToSession(FString& URL);

	/** A hard-coded encryption key used to try out the encryption code. This is NOT SECURE, do not use this technique in production! */
	// 用来测试的硬编码密钥
	TArray<uint8> DebugTestEncryptionKey;
};

#undef UE_API
