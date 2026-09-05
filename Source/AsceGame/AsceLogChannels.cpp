// Fill out your copyright notice in the Description page of Project Settings.


#include "AsceLogChannels.h"

#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY(LogAsce);
DEFINE_LOG_CATEGORY(LogAsceExperience);
DEFINE_LOG_CATEGORY(LogAsceAbilitySystem);
DEFINE_LOG_CATEGORY(LogAsceTeams);

//此函数获取传入的UObject是客户端生成的还是服务器生成的
//仅仅具有网络同步的对象（即Actor和Component）才能返回有效值，否则返回空字符串
FString GetClientServerContextString(UObject* ContextObject)
{
	ENetRole Role = ROLE_None;

	if (AActor* Actor = Cast<AActor>(ContextObject))
	{
		Role = Actor->GetLocalRole();
	}
	else if (UActorComponent* Component = Cast<UActorComponent>(ContextObject))
	{
		Role = Component->GetOwnerRole();
	}

	if (Role != ROLE_None)
	{
		return (Role == ROLE_Authority) ? TEXT("Server") : TEXT("Client");
	}
	else
	{
#if WITH_EDITOR
		if (GIsEditor)
		{
			extern ENGINE_API FString GPlayInEditorContextString;
			return GPlayInEditorContextString;
		}
#endif
	}

	return TEXT("[]");
}
