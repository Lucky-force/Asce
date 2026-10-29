// Fill out your copyright notice in the Description page of Project Settings.

#include "Cheat/Console/AsceCheatConsoleWidget.h"

#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include "Cheat/AsceCheatAction.h"
#include "Cheat/AsceCheatLibrary.h"
#include "Cheat/Components/AsceCheatComponentBase.h"

namespace
{
	void TokenizeInput(const FString& Input, TArray<FString>& OutTokens, bool& bOutTrailingSpace)
	{
		OutTokens.Reset();
		bOutTrailingSpace = Input.EndsWith(TEXT(" "));

		FString Trimmed = Input;
		Trimmed.TrimStartAndEndInline();
		if (!Trimmed.IsEmpty())
		{
			Trimmed.ParseIntoArrayWS(OutTokens);
		}
	}

	bool StartsWithIgnoreCase(const FString& Value, const FString& Prefix)
	{
		return Prefix.IsEmpty() || Value.StartsWith(Prefix, ESearchCase::IgnoreCase);
	}

	FString JoinCandidates(const TArray<FString>& Candidates)
	{
		return FString::Join(Candidates, TEXT("   "));
	}
}

void SAsceCheatConsole::Construct(const FArguments& InArgs)
{
	Component = InArgs._CheatComponent;
	OnCloseRequested = InArgs._OnCloseRequested;

	// 控制台打开时刷新一次动作发现（热更新/后加载的类也能进来；之后按 ActionBase 缓存）
	UAsceCheatLibrary::InvalidateActionCache();

	if (UAsceCheatComponentBase* Comp = Component.Get())
	{
		Comp->SetConsoleOutputSink(FAsceCheatConsoleOutput::CreateSP(this, &SAsceCheatConsole::HandleConsoleOutput));
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.85f))
		.Padding(8.f)
		[
			SNew(SVerticalBox)
			// 输出区
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SAssignNew(ScrollBox, SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(OutputBox, SVerticalBox)
				]
			]
			// 补全提示（半透明，不响应鼠标）
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 6.f, 0.f, 0.f)
			[
				SAssignNew(HintText, STextBlock)
				.ColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.55f)))
				.AutoWrapText(true)
			]
			// 输入行
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 2.f, 0.f, 0.f)
			[
				SAssignNew(InputBox, SEditableTextBox)
				.HintText(INVTEXT("输入命令（Tab 补全，↑↓ 历史，Esc 关闭）"))
				.OnTextChanged(this, &SAsceCheatConsole::OnInputChanged)
				.OnTextCommitted(this, &SAsceCheatConsole::OnInputCommitted)
			]
		]
	];

	AppendLine(TEXT("Asce 作弊控制台"), false);
	if (UAsceCheatComponentBase* Comp = Component.Get())
	{
		AppendLine(Comp->BuildActionListText(), false);
	}
	RefreshHint();
}

void SAsceCheatConsole::FocusInput() const
{
	if (InputBox.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(InputBox, EFocusCause::SetDirectly);
	}
}

void SAsceCheatConsole::AppendLine(const FString& Line, const bool bIsError)
{
	if (!OutputBox.IsValid())
	{
		return;
	}

	// 输出太多时清一次，避免长会话一直堆控件
	if (OutputBox->GetChildren()->Num() > 500)
	{
		OutputBox->ClearChildren();
	}

	OutputBox->AddSlot()
	.AutoHeight()
	.Padding(0.f, 1.f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Line))
		.ColorAndOpacity(FSlateColor(bIsError ? FLinearColor(1.f, 0.35f, 0.3f) : FLinearColor(0.92f, 0.92f, 0.92f)))
		.AutoWrapText(true)
	];

	if (ScrollBox.IsValid())
	{
		ScrollBox->ScrollToEnd();
	}
}

void SAsceCheatConsole::HandleConsoleOutput(const FString& Text, const bool bIsError)
{
	AppendLine(Text, bIsError);
}

const TMap<FName, FAsceCheatActionArg>& SAsceCheatConsole::GetActionArgs(const UAsceCheatAction* Action) const
{
	if (const TMap<FName, FAsceCheatActionArg>* Cached = ArgsCache.Find(Action))
	{
		return *Cached;
	}
	return ArgsCache.Add(Action, Action ? Action->BuildArgs() : TMap<FName, FAsceCheatActionArg>());
}

// =============================================================================================
// 输入 / 历史 / 关闭
// =============================================================================================

FReply SAsceCheatConsole::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Escape)
	{
		OnCloseRequested.ExecuteIfBound();
		return FReply::Handled();
	}

	if (Key == EKeys::Tab)
	{
		ApplyCompletion();
		return FReply::Handled();
	}

	// ↑/↓ 走历史
	if (Key == EKeys::Up && History.Num() > 0)
	{
		HistoryIndex = FMath::Clamp(HistoryIndex - 1, 0, History.Num() - 1);
		SetInputText(History[HistoryIndex]);
		RefreshHint();
		return FReply::Handled();
	}
	if (Key == EKeys::Down && History.Num() > 0)
	{
		HistoryIndex = FMath::Clamp(HistoryIndex + 1, 0, History.Num());
		SetInputText(HistoryIndex >= History.Num() ? FString() : History[HistoryIndex]);
		RefreshHint();
		return FReply::Handled();
	}

	return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

void SAsceCheatConsole::OnInputChanged(const FText& Text)
{
	RefreshHint();
}

void SAsceCheatConsole::OnInputCommitted(const FText& Text, const ETextCommit::Type CommitType)
{
	if (CommitType != ETextCommit::OnEnter)
	{
		return;
	}
	Submit(Text.ToString());
}

void SAsceCheatConsole::SetInputText(const FString& Text)
{
	if (InputBox.IsValid())
	{
		InputBox->SetText(FText::FromString(Text));
	}
}

void SAsceCheatConsole::Submit(const FString& RawLine)
{
	FString Line = RawLine;
	Line.TrimStartAndEndInline();
	if (Line.IsEmpty())
	{
		return;
	}

	History.Add(Line);
	HistoryIndex = History.Num();
	AppendLine(FString::Printf(TEXT("> %s"), *Line), false);
	SetInputText(FString());
	RefreshHint();

	UAsceCheatComponentBase* Comp = Component.Get();
	if (!Comp)
	{
		AppendLine(TEXT("作弊组件已失效"), true);
		return;
	}

	// ---- 内置作弊命令 ----
	if (Line.Equals(TEXT("asce.list"), ESearchCase::IgnoreCase))
	{
		AppendLine(Comp->BuildActionListText(), false);
		return;
	}
	if (Line.Equals(TEXT("asce.cheat"), ESearchCase::IgnoreCase))
	{
		AppendLine(TEXT("用法：asce.cheat <Action> [Key=Value ...]（动作名可以直接输，asce.list 看动作）"), true);
		return;
	}
	if (Line.StartsWith(TEXT("asce.cheat "), ESearchCase::IgnoreCase))
	{
		Comp->RequestLine(Line.RightChop(11));   // 主机直接跑；客户端转 Server RPC，结果回包到 sink
		return;
	}
	if (Line.StartsWith(TEXT("asce.grant "), ESearchCase::IgnoreCase))
	{
		FString Rest = Line.RightChop(11);
		TArray<FString> Tokens;
		Rest.ParseIntoArrayWS(Tokens);
		if (Tokens.Num() == 0)
		{
			AppendLine(TEXT("用法：asce.grant <索引|玩家名> [0|1]"), true);
			return;
		}
		const bool bAllowed = Tokens.Num() < 2 || Tokens[1] != TEXT("0");
		FString Error;
		if (!UAsceCheatLibrary::GrantCheat(Comp->GetWorld(), Tokens[0], bAllowed, Error))
		{
			AppendLine(Error, true);
		}
		return;
	}

	// ---- 动作名直接输（等价 `asce.cheat <Action> …`） ----
	{
		FString FirstToken;
		{
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() > 0)
			{
				FirstToken = Tokens[0];
			}
		}
		if (!FirstToken.IsEmpty() && UAsceCheatLibrary::FindAction(FName(*FirstToken), Comp->GetActionBaseClass()))
		{
			Comp->RequestLine(Line);
			return;
		}
	}

	// ---- 其它：交给引擎控制台（cvar / stat / 引擎命令） ----
	if (APlayerController* PC = Comp->GetOwnerPlayerController())
	{
		const FString Result = PC->ConsoleCommand(Line, /*bWriteToLog*/ true);
		if (!Result.IsEmpty())
		{
			AppendLine(Result, false);
		}
	}
	else
	{
		AppendLine(TEXT("没有本机 PlayerController，无法执行引擎命令"), true);
	}
}

// =============================================================================================
// 补全
// =============================================================================================

TArray<FString> SAsceCheatConsole::GetActionNames() const
{
	TArray<FString> Names;
	if (const UAsceCheatComponentBase* Comp = Component.Get())
	{
		for (const UAsceCheatAction* Action : UAsceCheatLibrary::GetActions(Comp->GetActionBaseClass()))
		{
			if (Action)
			{
				Names.Add(Action->GetActionName().ToString());
			}
		}
	}
	Names.Sort();
	return Names;
}

TArray<FString> SAsceCheatConsole::GetCommandNames() const
{
	TArray<FString> Names = GetActionNames();
	Names.Add(TEXT("asce.cheat"));
	Names.Add(TEXT("asce.console"));
	Names.Add(TEXT("asce.grant"));
	Names.Add(TEXT("asce.list"));
	Names.Sort();
	return Names;
}

FString SAsceCheatConsole::GetCurrentToken(int32& OutStart) const
{
	const FString Input = InputBox.IsValid() ? InputBox->GetText().ToString() : FString();

	int32 Start = Input.Len();
	if (!Input.EndsWith(TEXT(" ")))
	{
		int32 SpaceIndex = INDEX_NONE;
		Start = Input.FindLastChar(TEXT(' '), SpaceIndex) ? SpaceIndex + 1 : 0;
	}
	OutStart = Start;
	return Input.Mid(Start);
}

const UAsceCheatAction* SAsceCheatConsole::ResolveAction(const TArray<FString>& Tokens, int32& OutArgStart) const
{
	OutArgStart = 1;

	const UAsceCheatComponentBase* Comp = Component.Get();
	if (!Comp || Tokens.Num() == 0)
	{
		return nullptr;
	}
	const TSubclassOf<UAsceCheatAction> Base = Comp->GetActionBaseClass();
	if (!Base)
	{
		return nullptr;
	}

	if (Tokens[0].Equals(TEXT("asce.cheat"), ESearchCase::IgnoreCase))
	{
		OutArgStart = 2;
		return Tokens.Num() > 1 ? UAsceCheatLibrary::FindAction(FName(*Tokens[1]), Base) : nullptr;
	}

	return UAsceCheatLibrary::FindAction(FName(*Tokens[0]), Base);
}

TArray<FString> SAsceCheatConsole::GetCandidates() const
{
	TArray<FString> Result;

	int32 TokenStart = 0;
	const FString Token = GetCurrentToken(TokenStart);
	const FString Input = InputBox.IsValid() ? InputBox->GetText().ToString() : FString();

	TArray<FString> Tokens;
	bool bTrailingSpace = false;
	TokenizeInput(Input, Tokens, bTrailingSpace);

	// ① 命令位（还没输入过空格）
	if (Tokens.Num() <= 1 && !bTrailingSpace)
	{
		for (const FString& Name : GetCommandNames())
		{
			if (StartsWithIgnoreCase(Name, Token))
			{
				Result.Add(Name);
			}
		}
		return Result;
	}

	// ② `asce.cheat ` 之后：动作名（含还没开始输入的情况）
	const bool bAfterCheatPrefix = (Tokens.Num() == 1 && bTrailingSpace && Tokens[0].Equals(TEXT("asce.cheat"), ESearchCase::IgnoreCase))
		|| (Tokens.Num() == 2 && !bTrailingSpace && Tokens[0].Equals(TEXT("asce.cheat"), ESearchCase::IgnoreCase));
	if (bAfterCheatPrefix)
	{
		for (const FString& Name : GetActionNames())
		{
			if (StartsWithIgnoreCase(Name, Token))
			{
				Result.Add(Name);
			}
		}
		return Result;
	}

	// ③ 参数位
	int32 ArgStart = 1;
	const UAsceCheatAction* Action = ResolveAction(Tokens, ArgStart);
	if (!Action)
	{
		return Result;
	}

	const TMap<FName, FAsceCheatActionArg>& Args = GetActionArgs(Action);

	// 已给的参数
	TSet<FName> GivenKeys;
	for (int32 Index = ArgStart; Index < Tokens.Num(); ++Index)
	{
		FString Key;
		FString Value;
		if (Tokens[Index].Split(TEXT("="), &Key, &Value))
		{
			GivenKeys.Add(FName(*Key));
		}
	}

	// 正在输入 `Key=Value`
	FString Key;
	FString Value;
	if (Token.Split(TEXT("="), &Key, &Value))
	{
		const FAsceCheatActionArg* Arg = Args.Find(FName(*Key));
		const UWorld* World = Component.IsValid() ? Component->GetWorld() : nullptr;
		if (Arg && Arg->GetArgType())
		{
			const FString CacheKey = Action->GetActionName().ToString() + TEXT(".") + Key;
			const TArray<FString>* Suggestions = SuggestionCache.Find(CacheKey);
			if (!Suggestions)
			{
				Suggestions = &SuggestionCache.Add(CacheKey, Arg->GetArgType()->GetSuggestions(World));
			}
			for (const FString& Suggestion : *Suggestions)
			{
				if (StartsWithIgnoreCase(Suggestion, Value))
				{
					Result.Add(FString::Printf(TEXT("%s=%s"), *Key, *Suggestion));
				}
			}
		}
		return Result;
	}

	// 正在输入参数名（补 `Key=`）
	for (const TPair<FName, FAsceCheatActionArg>& Pair : Args)
	{
		if (GivenKeys.Contains(Pair.Key))
		{
			continue;
		}
		const FString Name = Pair.Key.ToString();
		if (StartsWithIgnoreCase(Name, Token))
		{
			Result.Add(Name + TEXT("="));
		}
	}
	return Result;
}

FString SAsceCheatConsole::BuildHintText() const
{
	const FString Input = InputBox.IsValid() ? InputBox->GetText().ToString() : FString();

	TArray<FString> Tokens;
	bool bTrailingSpace = false;
	TokenizeInput(Input, Tokens, bTrailingSpace);

	// 空输入：整体用法
	if (Tokens.Num() == 0)
	{
		return TEXT("asce.list  ·  asce.cheat <Action> …  ·  asce.grant <索引|玩家名> [0|1]  ·  asce.console")
			TEXT("      动作名可以直接输（Tab 补全）");
	}

	// 命令位
	if (Tokens.Num() <= 1 && !bTrailingSpace)
	{
		int32 TokenStart = 0;
		const FString Token = GetCurrentToken(TokenStart);
		const TArray<FString> Candidates = GetCandidates();
		return Candidates.Num() > 0
			? FString::Printf(TEXT("Tab：%s"), *JoinCandidates(Candidates))
			: FString::Printf(TEXT("未知命令 [%s]（asce.list 看动作，Tab 试试）"), *Token);
	}

	// 内置命令说明
	const FString Command = Tokens[0];
	if (Command.Equals(TEXT("asce.list"), ESearchCase::IgnoreCase))
	{
		return TEXT("列出当前模式的全部动作");
	}
	if (Command.Equals(TEXT("asce.console"), ESearchCase::IgnoreCase))
	{
		return TEXT("打开 / 关闭本控制台（Esc 也能关）");
	}
	if (Command.Equals(TEXT("asce.grant"), ESearchCase::IgnoreCase))
	{
		return TEXT("asce.grant <索引|玩家名> [0|1]   —— 仅主机，默认 1 = 开");
	}
	if (Command.Equals(TEXT("asce.cheat"), ESearchCase::IgnoreCase))
	{
		if (Tokens.Num() == 1 && !bTrailingSpace)
		{
			return TEXT("asce.cheat <Action> [Key=Value ...]   —— 也可以直接输动作名");
		}
		if (Tokens.Num() == 1 && bTrailingSpace)
		{
			return FString::Printf(TEXT("动作：%s"), *JoinCandidates(GetCandidates()));
		}
		if (Tokens.Num() == 2 && !bTrailingSpace)
		{
			const TArray<FString> Candidates = GetCandidates();
			return Candidates.Num() > 0
				? FString::Printf(TEXT("动作：%s"), *JoinCandidates(Candidates))
				: TEXT("没有匹配的动作（asce.list 看全部）");
		}
	}

	// 动作位：参数结构
	int32 ArgStart = 1;
	const UAsceCheatAction* Action = ResolveAction(Tokens, ArgStart);
	if (!Action)
	{
		return FString::Printf(TEXT("找不到动作 [%s]（asce.list 看可用动作）"), *Tokens[Tokens.Num() - 1]);
	}

	const TMap<FName, FAsceCheatActionArg>& Args = GetActionArgs(Action);

	TSet<FName> GivenKeys;
	for (int32 Index = ArgStart; Index < Tokens.Num(); ++Index)
	{
		FString Key;
		FString Value;
		if (Tokens[Index].Split(TEXT("="), &Key, &Value))
		{
			GivenKeys.Add(FName(*Key));
		}
	}

	// 稳定顺序（TMap 无序）
	TArray<FName> ArgNames;
	Args.GetKeys(ArgNames);
	ArgNames.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });

	FString Structure = Action->GetActionName().ToString();
	for (const FName& ArgName : ArgNames)
	{
		const FAsceCheatActionArg& Arg = Args.FindChecked(ArgName);
		const FAsceCheatArgType* ArgType = Arg.GetArgType();
		const bool bGiven = GivenKeys.Contains(ArgName);

		Structure += TEXT("   ");
		Structure += ArgName.ToString();
		Structure += TEXT(":");
		Structure += ArgType ? ArgType->GetTypeName() : TEXT("?");
		if (bGiven)
		{
			Structure += TEXT("✓");
		}
		else if (Arg.bRequired)
		{
			Structure += TEXT("*");
		}
		if (!bGiven && !Arg.ArgContent.IsEmpty())
		{
			Structure += FString::Printf(TEXT("=%s"), *Arg.ArgContent);
		}
	}

	const TArray<FString> Candidates = GetCandidates();
	if (Candidates.Num() > 0)
	{
		return Structure + TEXT("\nTab：") + JoinCandidates(Candidates);
	}
	return Structure;
}

void SAsceCheatConsole::RefreshHint()
{
	if (!HintText.IsValid())
	{
		return;
	}

	const FString Hint = BuildHintText();
	HintText->SetText(FText::FromString(Hint));
	HintText->SetVisibility(Hint.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
}

void SAsceCheatConsole::ApplyCompletion()
{
	TArray<FString> Candidates = GetCandidates();
	if (Candidates.Num() == 0)
	{
		return;
	}

	int32 Start = 0;
	const FString Token = GetCurrentToken(Start);
	const FString Input = InputBox.IsValid() ? InputBox->GetText().ToString() : FString();

	FString Completion = Candidates[0];
	if (Candidates.Num() > 1)
	{
		// 多个候选：先补到公共前缀
		for (const FString& Candidate : Candidates)
		{
			int32 Index = 0;
			while (Index < Completion.Len() && Index < Candidate.Len()
				&& FChar::ToLower(Completion[Index]) == FChar::ToLower(Candidate[Index]))
			{
				++Index;
			}
			Completion.LeftInline(Index);
		}

		if (Completion.Len() <= Token.Len())
		{
			// 没有更多可补：列出候选（相当于 bash 的连按两次 Tab）
			AppendLine(JoinCandidates(Candidates), false);
			RefreshHint();
			return;
		}
	}
	else if (Completion.Len() <= Token.Len() && Completion.Equals(Token, ESearchCase::IgnoreCase))
	{
		return;
	}

	FString NewInput = Input.Left(Start) + Completion;
	// 命令 / 参数名后补一个空格（`Key=` 和 `Key=Value` 不补）
	if (!Completion.Contains(TEXT("=")))
	{
		NewInput += TEXT(" ");
	}
	SetInputText(NewInput);
	RefreshHint();
}
