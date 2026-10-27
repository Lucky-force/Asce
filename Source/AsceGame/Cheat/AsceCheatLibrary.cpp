// Fill out your copyright notice in the Description page of Project Settings.

#include "Cheat/AsceCheatLibrary.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Cheat/AsceCheatDefines.h"

#include "Cheat/AsceCheatAction.h"
#include "Cheat/Components/AsceCheatComponentBase.h"
#include "UObject/UObjectHash.h"
#include "Player/AscePlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceCheatLibrary)

namespace
{
	/** 运行时开关：默认"Shipping 关、其它开"；内部包可在 ini 里开 */
	TAutoConsoleVariable<int32> CVarAsceCheatEnable(
		TEXT("Asce.Cheat.Enable"),
		UE_BUILD_SHIPPING ? 0 : 1,
		TEXT("1 = 允许执行 Asce 作弊（权威端 + per-player 权限；见 Cheat/作弊系统设计.md）"),
		ECVF_Default);
}

bool UAsceCheatLibrary::IsCheatEnabled()
{
#if !ASCE_CHEAT_ENABLED
	return false;
#else
	return CVarAsceCheatEnable.GetValueOnGameThread() != 0;
#endif
}

TArray<UAsceCheatAction*> UAsceCheatLibrary::GetActions(const TSubclassOf<UAsceCheatAction> ActionBase)
{
	const UClass* BaseClass = ActionBase ? ActionBase.Get() : UAsceCheatAction::StaticClass();
	if (!BaseClass)
	{
		return {};
	}

	TArray<UClass*> Classes;
	GetDerivedClasses(BaseClass, Classes, /*bRecursive*/ true);

	TArray<UAsceCheatAction*> Result;
	for (UClass* Class : Classes)
	{
		if (!Class || Class->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}
		UAsceCheatAction* Action = Cast<UAsceCheatAction>(Class->GetDefaultObject());
		if (!Action || Action->GetActionName().IsNone())
		{
			continue;
		}
		Result.Add(Action);
	}

	// 稳定顺序：先按分类、再按显示名（UI 分组 / 控制台列表都好看）
	Result.Sort([](const UAsceCheatAction& A, const UAsceCheatAction& B)
	{
		const FString CategoryA = A.GetCategory().ToString();
		const FString CategoryB = B.GetCategory().ToString();
		if (CategoryA != CategoryB)
		{
			return CategoryA < CategoryB;
		}
		return A.GetDisplayName().ToString() < B.GetDisplayName().ToString();
	});
	return Result;
}

UAsceCheatAction* UAsceCheatLibrary::FindAction(const FName ActionName, const TSubclassOf<UAsceCheatAction> ActionBase)
{
	for (UAsceCheatAction* Action : GetActions(ActionBase))
	{
		if (Action && Action->GetActionName() == ActionName)
		{
			return Action;
		}
	}
	return nullptr;
}

FString UAsceCheatLibrary::BuildActionListText(const TSubclassOf<UAsceCheatAction> ActionBase)
{
	FString Text;
	for (const UAsceCheatAction* Action : GetActions(ActionBase))
	{
		if (!Action)
		{
			continue;
		}
		Text += FString::Printf(TEXT("\n%s  [%s]  %s"), *Action->GetActionName().ToString(),
			*Action->GetCategory().ToString(), *Action->GetDisplayName().ToString());

		const TMap<FName, FAsceCheatActionArg> Args = Action->BuildArgs();
		if (Args.Num() == 0)
		{
			continue;
		}
		Text += TEXT("\n    参数：");
		for (const TPair<FName, FAsceCheatActionArg>& Pair : Args)
		{
			const FAsceCheatArgType* ArgType = Pair.Value.GetArgType();
			Text += FString::Printf(TEXT(" %s:%s%s"), *Pair.Key.ToString(),
				ArgType ? *ArgType->GetTypeName() : TEXT("?"),
				Pair.Value.bRequired ? TEXT("*") : TEXT(""));
		}
	}
	return Text.IsEmpty() ? TEXT("（该模式下还没有注册任何动作）") : Text;
}

bool UAsceCheatLibrary::Run(UObject* WorldContextObject, APlayerController* Requester, UAsceCheatAction* Action,
	const TMap<FName, FString>& ProvidedArgs, FString& OutSummary, FString& OutError)
{
	OutSummary.Reset();
	OutError.Reset();

	if (!Action)
	{
		OutError = TEXT("动作为空");
		return false;
	}

	// ① 动作声明（带默认值）② 输入覆盖 ③ 校验
	TMap<FName, FAsceCheatActionArg> Args = Action->BuildArgs();
	for (const TPair<FName, FString>& Given : ProvidedArgs)
	{
		FAsceCheatActionArg* Slot = Args.Find(Given.Key);
		if (!Slot)
		{
			OutError = FString::Printf(TEXT("动作 [%s] 没有参数 [%s]"),
				*Action->GetActionName().ToString(), *Given.Key.ToString());
			return false;
		}
		Slot->ArgContent = Given.Value;
	}
	for (const TPair<FName, FAsceCheatActionArg>& Pair : Args)
	{
		const FAsceCheatActionArg& Arg = Pair.Value;
		if (Arg.bRequired && Arg.ArgContent.IsEmpty())
		{
			OutError = FString::Printf(TEXT("缺少必填参数 [%s]"), *Pair.Key.ToString());
			return false;
		}
		const FAsceCheatArgType* ArgType = Arg.GetArgType();
		if (!ArgType)
		{
			OutError = FString::Printf(TEXT("参数 [%s] 没有声明类型"), *Pair.Key.ToString());
			return false;
		}
		FString TypeError;
		if (!ArgType->Validate(Arg.ArgContent, TypeError))
		{
			OutError = FString::Printf(TEXT("参数 [%s] 非法：%s"), *Pair.Key.ToString(), *TypeError);
			return false;
		}
	}

	FAsceCheatContext Context;
	Context.World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	Context.Requester = Requester;
	if (const AAscePlayerState* PlayerState = Requester ? Requester->GetPlayerState<AAscePlayerState>() : nullptr)
	{
		Context.RequesterTeamId = PlayerState->GetTeamId();
	}

	return Action->Execute(Context, Args, OutSummary, OutError);
}

bool UAsceCheatLibrary::RunFromLine(UObject* WorldContextObject, APlayerController* Requester,
	const TSubclassOf<UAsceCheatAction> ActionBase, const FString& Line, FString& OutSummary, FString& OutError)
{
	OutSummary.Reset();
	OutError.Reset();

	FString Trimmed = Line;
	Trimmed.TrimStartAndEndInline();
	TArray<FString> Tokens;
	Trimmed.ParseIntoArrayWS(Tokens);
	if (Tokens.Num() == 0)
	{
		OutError = TEXT("空命令（用法：asce.cheat <Action> [Key=Value ...]）");
		return false;
	}

	const FName ActionName = FName(*Tokens[0]);
	TMap<FName, FString> ProvidedArgs;
	for (int32 Index = 1; Index < Tokens.Num(); ++Index)
	{
		FString Key;
		FString Value;
		if (!Tokens[Index].Split(TEXT("="), &Key, &Value))
		{
			OutError = FString::Printf(TEXT("参数 [%s] 必须是 Key=Value 形式"), *Tokens[Index]);
			return false;
		}
		ProvidedArgs.Add(FName(*Key), Value);
	}

	UAsceCheatAction* Action = FindAction(ActionName, ActionBase);
	if (!Action)
	{
		OutError = FString::Printf(TEXT("找不到动作 [%s]（用 asce.list 看可用动作）"), *ActionName.ToString());
		return false;
	}

	return Run(WorldContextObject, Requester, Action, ProvidedArgs, OutSummary, OutError);
}
bool UAsceCheatLibrary::GrantCheat(UWorld* World, const FString& NameOrIndex, const bool bAllowed, FString& OutError)
{
	OutError.Reset();

	if (!World || World->GetNetMode() == NM_Client)
	{
		OutError = TEXT("只能在服务器上授权");
		return false;
	}

	AGameStateBase* GameState = World->GetGameState();
	if (!GameState)
	{
		OutError = TEXT("没有 GameState");
		return false;
	}

	// 目标：纯数字 = PlayerArray 索引；否则按玩家名（大小写不敏感）
	const FString Key = NameOrIndex.TrimStartAndEnd();
	APlayerState* Target = nullptr;
	if (Key.IsNumeric())
	{
		const int32 Index = FCString::Atoi(*Key);
		if (GameState->PlayerArray.IsValidIndex(Index))
		{
			Target = GameState->PlayerArray[Index];
		}
	}
	if (!Target)
	{
		for (const TObjectPtr<APlayerState>& PlayerState : GameState->PlayerArray)
		{
			if (PlayerState && PlayerState->GetPlayerName().Equals(Key, ESearchCase::IgnoreCase))
			{
				Target = PlayerState;
				break;
			}
		}
	}

	if (!Target)
	{
		OutError = FString::Printf(TEXT("找不到玩家 [%s]（用法：<索引|玩家名> [0|1]）"), *Key);
		return false;
	}

	UAsceCheatComponentBase* Component = Target->FindComponentByClass<UAsceCheatComponentBase>();
	if (!Component)
	{
		OutError = FString::Printf(TEXT("玩家 [%s] 没有作弊组件"), *Target->GetPlayerName());
		return false;
	}

	Component->SetCheatAllowed(bAllowed);
	UE_LOG(LogTemp, Log, TEXT("[Cheat] 玩家 [%s] 作弊权限 -> %s"), *Target->GetPlayerName(), bAllowed ? TEXT("开") : TEXT("关"));
	return true;
}

void UAsceCheatLibrary::ToggleConsoleForLocalPlayer(UObject* WorldContextObject)
{
	if (!IsCheatEnabled())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Cheat] 作弊未启用（Asce.Cheat.Enable=0）"));
		return;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APlayerState* PlayerState = PC ? PC->GetPlayerState<APlayerState>() : nullptr;
	UAsceCheatComponentBase* Component = PlayerState ? PlayerState->FindComponentByClass<UAsceCheatComponentBase>() : nullptr;
	if (!Component)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Cheat] 当前模式没有作弊组件（该模式的 GameFeature 未激活？）"));
		return;
	}
	Component->ToggleConsole();
}
