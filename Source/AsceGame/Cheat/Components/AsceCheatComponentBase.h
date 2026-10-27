// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Cheat/AsceCheatAction.h"
#include "Components/PlayerStateComponent.h"

#include "AsceCheatComponentBase.generated.h"

class APlayerController;
class SAsceCheatConsole;

/** Slate 控制台的输出回调：`Text` 已格式化好一行，`bIsError` 决定颜色 */
DECLARE_DELEGATE_TwoParams(FAsceCheatConsoleOutput, const FString& /*Text*/, bool /*bIsError*/);

/**
 * **作弊组件的公共基类**（每个模式派生一个，如 `UAsceRTSCheatComponent`）。
 *
 * 职责（与模式无关的部分）：
 * - `bCheatAllowed`：per-player 权限开关，服务端权威、`COND_OwnerOnly` 复制给属主；
 * - **请求路由**：`RequestLine` → 权威端直接执行，客户端转 `Server_RunCheatLine`；
 * - **结果回显**：结果统一打回**发起者本机控制台**（`Client_CheatResult`）；
 * - 子类只需给"**这个模式的动作基类**"和"**这个模式的函数库入口**"。
 *
 * **挂载方式**：由 GameFeature 的 `UGameFeatureAction_AddComponents` 添加到 `AModularPlayerState`
 * （复制组件 ⇒ 只在权威端创建、组件名 = 类名、客户端靠子对象复制拿到同名组件 ⇒ RPC 双向可用）。
 * 见 `Cheat/作弊系统设计.md` §6.1。
 */
UCLASS(Abstract)
class ASCEGAME_API UAsceCheatComponentBase : public UPlayerStateComponent
{
	GENERATED_BODY()

public:
	UAsceCheatComponentBase(const FObjectInitializer& ObjectInitializer);
	/** 在 cpp 定义：成员 TSharedPtr<SAsceCheatConsole> 需要完整类型 */
	virtual ~UAsceCheatComponentBase() override;

	/** **本端**是否允许使用作弊（UI 用：主机/单机本机恒真；客户端读自己 `bCheatAllowed`） */
	UFUNCTION(BlueprintPure, Category = "Asce|Cheat")
	bool IsCheatAvailable() const;

	/** 是否被主机授权（服务端权威；客户端读复制值） */
	UFUNCTION(BlueprintPure, Category = "Asce|Cheat")
	bool IsCheatAllowed() const { return bCheatAllowed; }

	/** **仅服务端**：设置权限（主机授权入口最终走到这里） */
	void SetCheatAllowed(bool bAllowed);

	/** 请求执行一行（UI / 控制台入口）：权威端直接跑，客户端转 Server RPC */
	UFUNCTION(BlueprintCallable, Category = "Asce|Cheat")
	void RequestLine(const FString& Line);

	/** 本模式的动作基类（列表/执行过滤用） */
	virtual TSubclassOf<UAsceCheatAction> GetActionBaseClass() const PURE_VIRTUAL(UAsceCheatComponentBase::GetActionBaseClass, return nullptr;);

	/** 本模式的执行入口（子类覆写 → 调本模式函数库） */
	virtual bool ExecuteLine(const FString& Line, FString& OutSummary, FString& OutError)
		PURE_VIRTUAL(UAsceCheatComponentBase::ExecuteLine, return false;);

	/** 本模式的动作列表文本（控制台 `asce.list`） */
	FString BuildActionListText() const;

	/** 属主的 PlayerController（服务端/客户端都可用；没有返回 nullptr） */
	APlayerController* GetOwnerPlayerController() const;

	/** **开关 Slate 控制台**（本机玩家；建议在 GameFeature 的输入里绑一个按键调它） */
	UFUNCTION(BlueprintCallable, Category = "Asce|Cheat")
	void ToggleConsole();

	UFUNCTION(BlueprintCallable, Category = "Asce|Cheat")
	void OpenConsole();

	UFUNCTION(BlueprintCallable, Category = "Asce|Cheat")
	void CloseConsole();

	/** Slate 控制台注册输出回调（关掉时传空 delegate 清掉） */
	void SetConsoleOutputSink(FAsceCheatConsoleOutput InSink);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	/** 执行并把结果打到本机控制台（Slate 面板打开时也同步给它） */
	void ExecuteAndReport(const FString& Line);

	/** 统一输出：日志 + （若有）Slate 面板 */
	void ReportOutput(const FString& Text, bool bIsError);

	/** 客户端 → 服务端：请求执行一行 */
	UFUNCTION(Server, Reliable)
	void Server_RunCheatLine(const FString& Line);

	/** 服务端 → 发起客户端：回传结果（由发起端打印） */
	UFUNCTION(Client, Reliable)
	void Client_CheatResult(const FString& Line, const FString& Summary, const FString& Error);

	UFUNCTION()
	void OnRep_CheatAllowed();

	UPROPERTY(ReplicatedUsing = OnRep_CheatAllowed)
	bool bCheatAllowed = false;

private:
	/** 本机的 Slate 控制台（只在本机玩家上创建；关掉即 Reset） */
	TSharedPtr<SAsceCheatConsole> ConsoleWidget;

	/** Slate 控制台的输出回调（本机执行 / 服务端回包都走它） */
	FAsceCheatConsoleOutput ConsoleOutput;
};
