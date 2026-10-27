// Fill out your copyright notice in the Description page of Project Settings.

#include "Cheat/Components/AsceCheatComponentBase.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

#include "Cheat/AsceCheatAction.h"
#include "Cheat/Console/AsceCheatConsoleWidget.h"
#include "Cheat/AsceCheatLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceCheatComponentBase)

UAsceCheatComponentBase::UAsceCheatComponentBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 权限开关要复制给属主（同时决定"只在权威端创建"⇒ 组件名=类名，客户端子对象复制拿到）
	SetIsReplicatedByDefault(true);
}

UAsceCheatComponentBase::~UAsceCheatComponentBase() = default;

void UAsceCheatComponentBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UAsceCheatComponentBase, bCheatAllowed, COND_OwnerOnly);
}

APlayerController* UAsceCheatComponentBase::GetOwnerPlayerController() const
{
	const APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	return PlayerState ? PlayerState->GetPlayerController() : nullptr;
}

bool UAsceCheatComponentBase::IsCheatAvailable() const
{
	if (!UAsceCheatLibrary::IsCheatEnabled())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (World->GetNetMode() == NM_Client)
	{
		return bCheatAllowed;   // 客户端：看自己有没有被授权
	}

	// 权威端：只有"属主本机"（主机 / 单机）才算
	const APlayerController* PC = GetOwnerPlayerController();
	return PC && PC->IsLocalController();
}

void UAsceCheatComponentBase::SetCheatAllowed(const bool bAllowed)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bCheatAllowed == bAllowed)
	{
		return;
	}
	bCheatAllowed = bAllowed;
	OnRep_CheatAllowed();   // 服务端主动刷一次；客户端走 OnRep
}

void UAsceCheatComponentBase::OnRep_CheatAllowed()
{
	UE_LOG(LogTemp, Log, TEXT("[Cheat] %s 作弊权限 %s"),
		*GetNameSafe(GetOwner()), bCheatAllowed ? TEXT("已开启") : TEXT("已关闭"));
}

void UAsceCheatComponentBase::RequestLine(const FString& Line)
{
	if (!IsCheatAvailable())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Cheat] 没有作弊权限（需要：权威端/属主本机，或主机 asce.grant 授权）"));
		return;
	}

	if (const AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
	{
		ExecuteAndReport(Line);
		return;
	}

	Server_RunCheatLine(Line);
}

void UAsceCheatComponentBase::ExecuteAndReport(const FString& Line)
{
	FString Summary;
	FString Error;
	const bool bOk = ExecuteLine(Line, Summary, Error);
	ReportOutput(FString::Printf(TEXT("%s ⇒ %s"), *Line, bOk ? (Summary.IsEmpty() ? TEXT("OK") : *Summary) : *Error), !bOk);
}

void UAsceCheatComponentBase::Server_RunCheatLine_Implementation(const FString& Line)
{
	if (!bCheatAllowed)
	{
		Client_CheatResult(Line, FString(), TEXT("没有作弊权限（需要主机 asce.grant 授权）"));
		return;
	}

	FString Summary;
	FString Error;
	const bool bOk = ExecuteLine(Line, Summary, Error);
	Client_CheatResult(Line, bOk ? Summary : FString(), bOk ? FString() : Error);
}

void UAsceCheatComponentBase::Client_CheatResult_Implementation(const FString& Line, const FString& Summary, const FString& Error)
{
	const bool bOk = Error.IsEmpty();
	ReportOutput(FString::Printf(TEXT("%s ⇒ %s"), *Line, bOk ? (Summary.IsEmpty() ? TEXT("OK") : *Summary) : *Error), !bOk);
}

FString UAsceCheatComponentBase::BuildActionListText() const
{
	return UAsceCheatLibrary::BuildActionListText(GetActionBaseClass());
}
void UAsceCheatComponentBase::SetConsoleOutputSink(FAsceCheatConsoleOutput InSink)
{
	ConsoleOutput = MoveTemp(InSink);
}

void UAsceCheatComponentBase::ReportOutput(const FString& Text, const bool bIsError)
{
	if (bIsError)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Cheat] %s"), *Text);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[Cheat] %s"), *Text);
	}

	if (ConsoleOutput.IsBound())
	{
		ConsoleOutput.Execute(Text, bIsError);
	}
}

void UAsceCheatComponentBase::ToggleConsole()
{
	if (ConsoleWidget.IsValid())
	{
		CloseConsole();
	}
	else
	{
		OpenConsole();
	}
}

void UAsceCheatComponentBase::OpenConsole()
{
	if (ConsoleWidget.IsValid())
	{
		return;
	}

	if (!IsCheatAvailable())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Cheat] 没有作弊权限，无法打开控制台"));
		return;
	}

	APlayerController* PC = GetOwnerPlayerController();
	UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr;
	if (!PC || !PC->IsLocalController() || !Viewport || !FSlateApplication::IsInitialized())
	{
		return;
	}

	ConsoleWidget = SNew(SAsceCheatConsole)
		.CheatComponent(this)
		.OnCloseRequested(FSimpleDelegate::CreateUObject(this, &UAsceCheatComponentBase::CloseConsole));

	Viewport->AddViewportWidgetContent(ConsoleWidget.ToSharedRef(), /*ZOrder*/ 1000);

	// 输入：打字给面板，鼠标仍可点（和 RTS 的 GameAndUI 一致）；关闭时恢复
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(ConsoleWidget);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(InputMode);
	PC->bShowMouseCursor = true;
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);

	ConsoleWidget->FocusInput();
}

void UAsceCheatComponentBase::CloseConsole()
{
	if (!ConsoleWidget.IsValid())
	{
		return;
	}

	APlayerController* PC = GetOwnerPlayerController();

	// 先摘掉输出回调，避免面板销毁后还被回调
	ConsoleOutput.Unbind();
	if (UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr)
	{
		Viewport->RemoveViewportWidgetContent(ConsoleWidget.ToSharedRef());
	}
	ConsoleWidget.Reset();

	if (PC && PC->IsLocalController())
	{
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);

		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
	}
}

void UAsceCheatComponentBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseConsole();
	Super::EndPlay(EndPlayReason);
}
