// Fill out your copyright notice in the Description page of Project Settings.

#include "Cheat/AsceCheatDefines.h"

#if ASCE_CHEAT_ENABLED

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"

#include "Cheat/AsceCheatLibrary.h"
#include "Cheat/Components/AsceCheatComponentBase.h"

/**
 * 控制台入口（**Development / PIE 用**）：
 *   `asce.cheat <Action> [Key=Value ...]`  —— 执行一条作弊
 *   `asce.list`                            —— 列出可用动作
 *   `asce.grant <索引|玩家名> [0|1]`       —— **仅主机**：给某个玩家开关作弊权限（默认开）
 *
 * 本文件只做"找组件 + 转给它"：权限、路由、结果回显都在 `UAsceCheatComponentBase` 里；
 * 与 Slate 控制台 UI 共用同一条路径。
 *
 * 命令用 `ECVF_Default`（不标 Cheat）：全构建都编（`ASCE_CHEAT_ENABLED` 默认 1），
 * 权限只看组件上的 `bCheatAllowed`（主机启动时可由 cvar 给自己授权，客户端由主机 `asce.grant`）。
 */
namespace
{
	/** 找到"本机玩家"的作弊组件（各模式统一用基类找） */
	UAsceCheatComponentBase* FindLocalCheatComponent(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		APlayerState* PlayerState = PC ? PC->GetPlayerState<APlayerState>() : nullptr;
		return PlayerState ? PlayerState->FindComponentByClass<UAsceCheatComponentBase>() : nullptr;
	}

	/** 权威端兜底：随便找一个玩家身上的作弊组件（专用服务器控制台用） */
	UAsceCheatComponentBase* FindAnyCheatComponent(UWorld* World)
	{
		const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
		if (!GameState)
		{
			return nullptr;
		}
		for (const TObjectPtr<APlayerState>& PlayerState : GameState->PlayerArray)
		{
			if (UAsceCheatComponentBase* Component = PlayerState ? PlayerState->FindComponentByClass<UAsceCheatComponentBase>() : nullptr)
			{
				return Component;
			}
		}
		return nullptr;
	}

	void AsceCheatConsole_Cheat(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		const FString Line = FString::Join(Args, TEXT(" "));

		if (UAsceCheatComponentBase* Component = FindLocalCheatComponent(World))
		{
			Component->RequestLine(Line);   // 主机直接跑；客户端自动转 Server RPC
			return;
		}

		// 权威端但没有本机玩家（专用服务器控制台）：直接用某个玩家的组件执行
		if (World->GetNetMode() != NM_Client)
		{
			if (UAsceCheatComponentBase* Component = FindAnyCheatComponent(World))
			{
				FString Summary;
				FString Error;
				if (Component->ExecuteLine(Line, false, FVector::ZeroVector, Summary, Error))
				{
					UE_LOG(LogTemp, Log, TEXT("[Cheat] %s ⇒ %s"), *Line, Summary.IsEmpty() ? TEXT("OK") : *Summary);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[Cheat] %s ⇒ 失败：%s"), *Line, *Error);
				}
				return;
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("[Cheat] 当前模式没有作弊组件（该模式的 GameFeature 未激活？）"));
	}

	void AsceCheatConsole_List(const TArray<FString>& Args, UWorld* World)
	{
		UAsceCheatComponentBase* Component = FindLocalCheatComponent(World);
		if (!Component)
		{
			Component = FindAnyCheatComponent(World);
		}
		if (!Component)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Cheat] 当前模式没有作弊组件（该模式的 GameFeature 未激活？）"));
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("[Cheat] 可用动作：%s"), *Component->BuildActionListText());
	}

	void AsceCheatConsole_Grant(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Cheat] 只能在服务器上授权"));
			return;
		}
		if (Args.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Cheat] 用法：asce.grant <索引|玩家名> [0|1]"));
			return;
		}

		const bool bAllowed = Args.Num() < 2 || Args[1] != TEXT("0");

		FString Error;
		if (!UAsceCheatLibrary::GrantCheat(World, Args[0], bAllowed, Error))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Cheat] %s"), *Error);
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("[Cheat] 已%s玩家 [%s] 的作弊权限"),
			bAllowed ? TEXT("开启") : TEXT("关闭"), *Args[0]);
	}

	FAutoConsoleCommandWithWorldAndArgs GAsceCheatCommand(
		TEXT("asce.cheat"),
		TEXT("执行一条 Asce 作弊：asce.cheat <Action> [Key=Value ...]（asce.list 看可用动作）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsceCheatConsole_Cheat),
		ECVF_Default);

	FAutoConsoleCommandWithWorldAndArgs GAsceCheatGrantCommand(
		TEXT("asce.grant"),
		TEXT("仅主机：给某个玩家开关作弊权限 —— asce.grant <索引|玩家名> [0|1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsceCheatConsole_Grant),
		ECVF_Default);

	void AsceCheatConsole_Toggle(const TArray<FString>& Args, UWorld* World)
	{
		UAsceCheatLibrary::ToggleConsoleForLocalPlayer(World);
	}

	FAutoConsoleCommandWithWorldAndArgs GAsceCheatConsoleCommand(
		TEXT("asce.console"),
		TEXT("打开/关闭 Asce Slate 控制台（本机玩家；Shipping 也能用）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsceCheatConsole_Toggle),
		ECVF_Default);

	FAutoConsoleCommandWithWorldAndArgs GAsceCheatListCommand(
		TEXT("asce.list"),
		TEXT("列出所有 Asce 作弊动作"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsceCheatConsole_List),
		ECVF_Default);
}

#endif // ASCE_CHEAT_ENABLED
