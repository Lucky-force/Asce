// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SScrollBox;
class STextBlock;
class SVerticalBox;
class UAsceCheatAction;
class UAsceCheatComponentBase;

/**
 * **Slate 作弊控制台**（Shipping 也能用：不依赖 `~`/`CheatManager`）。
 *
 * - 输入一行 → `asce.*` 由作弊组件处理，动作名可以**直接输**（= `asce.cheat`），其它交给 `PC->ConsoleCommand(Line)`；
 * - **动态补全**：输入框上方一条半透明提示，按当前输入给出"下一个该填什么"（命令 → 动作 → 参数名 → 候选值），
 *   已填的参数打 ✓、必填未填的标 `*`；
 * - `Tab` 补全（唯一候选直接补；多个补公共前缀并在输出区列出），`↑`/`↓` 历史，`Esc` 关闭；
 * - 输出：本机执行结果和**服务端回包**（客户端）都流到这里（组件输出 sink）。
 *
 * 由 `UAsceCheatComponentBase::ToggleConsole()` 创建/销毁；**开关键绑在 GameFeature 的输入里**。
 *
 * 见 `Cheat/作弊系统设计.md` §7。
 */
class ASCEGAME_API SAsceCheatConsole : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAsceCheatConsole) {}
		/** 归属的作弊组件（通常就是本机玩家 PlayerState 上那个） */
		SLATE_ARGUMENT(TWeakObjectPtr<UAsceCheatComponentBase>, CheatComponent)
		/** 请求关闭（组件负责从 viewport 移除并恢复输入模式） */
		SLATE_EVENT(FSimpleDelegate, OnCloseRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** 把键盘焦点给输入框（打开控制台时调用） */
	void FocusInput() const;

	/** 追加一行输出（`bIsError` 红色） */
	void AppendLine(const FString& Line, bool bIsError);

	//~ Begin SWidget interface
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	//~ End SWidget interface

private:
	void OnInputCommitted(const FText& Text, ETextCommit::Type CommitType);
	void OnInputChanged(const FText& Text);
	void Submit(const FString& RawLine);
	void SetInputText(const FString& Text);

	/** 组件回调：任何路径的执行结果（含服务端回包） */
	void HandleConsoleOutput(const FString& Text, bool bIsError);

	// ---- 补全 / 提示 ----

	/** 正在输入的 token 及其在输入串里的起点 */
	FString GetCurrentToken(int32& OutStart) const;

	/** 解析出当前要补全的"槽位"：动作（可能为空）+ 参数起点下标 */
	const UAsceCheatAction* ResolveAction(const TArray<FString>& Tokens, int32& OutArgStart) const;

	/** 当前槽位的候选（完整 token 形式，如 `SpawnUnits` / `Count=` / `Team=2`） */
	TArray<FString> GetCandidates() const;

	/** 提示行（"下一个该填什么"）：命令位列命令、参数位列结构 + ✓/* 标记 + 当前候选 */
	FString BuildHintText() const;

	void RefreshHint();
	void ApplyCompletion();

	/** 所有可补全的 `命令`（动作名 + 内置命令），已按名字排序 */
	TArray<FString> GetCommandNames() const;

	/** 只有动作名（按名字排序） */
	TArray<FString> GetActionNames() const;

	TWeakObjectPtr<UAsceCheatComponentBase> Component;
	FSimpleDelegate OnCloseRequested;

	TSharedPtr<SScrollBox> ScrollBox;
	TSharedPtr<SVerticalBox> OutputBox;
	TSharedPtr<SEditableTextBox> InputBox;
	TSharedPtr<STextBlock> HintText;

	/** 命令历史（↑/↓） */
	TArray<FString> History;
	int32 HistoryIndex = 0;
};
