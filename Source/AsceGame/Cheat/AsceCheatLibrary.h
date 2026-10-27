// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "Cheat/AsceCheatAction.h"
#include "Cheat/AsceCheatTypes.h"

#include "AsceCheatLibrary.generated.h"

class APlayerController;

/**
 * 作弊的**通用能力库**（静态函数，不是 Subsystem —— 各模式的组件直接调它 / 调自己模式的库）。
 *
 * - **自动发现**：`GetDerivedClasses`，按模式的动作基类过滤（`ActionBase`），零注册；
 * - **统一解析/校验**：`RunFromLine` 把 `"SpawnUnits DefRow=101 X=1000"` 解析成 `Key=Value`，
 *   再"动作默认值 → 输入覆盖 → `ArgType->Validate`"；
 * - **不判权限**：能不能作弊由挂到 PlayerState 上的组件决定（见 `Components/AsceCheatComponentBase.h`），
 *   这样本库与模式/激活方式完全解耦。
 *
 * 见 `Cheat/作弊系统设计.md`。
 */
UCLASS()
class ASCEGAME_API UAsceCheatLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 运行时开关：cvar `Asce.Cheat.Enable`（Shipping 默认 0） */
	static bool IsCheatEnabled();

	/** 某模式的全部已注册动作（CDO），按 `分类 + 显示名` 排序 —— UI 枚举用 */
	UFUNCTION(BlueprintCallable, Category = "Asce|Cheat")
	static TArray<UAsceCheatAction*> GetActions(TSubclassOf<UAsceCheatAction> ActionBase);

	/** 按名字找动作（找不到返回 nullptr） */
	static UAsceCheatAction* FindAction(FName ActionName, TSubclassOf<UAsceCheatAction> ActionBase);

	/** 纯文本动作列表（控制台 `asce.list` 用） */
	UFUNCTION(BlueprintCallable, Category = "Asce|Cheat")
	static FString BuildActionListText(TSubclassOf<UAsceCheatAction> ActionBase);

	/** 执行一个动作（`ProvidedArgs` = 参数名 → 文本内容；没给的使用动作默认值） */
	static bool Run(UObject* WorldContextObject, APlayerController* Requester, UAsceCheatAction* Action,
		const TMap<FName, FString>& ProvidedArgs, FString& OutSummary, FString& OutError);

	/** 解析一整行再执行（控制台 / 命令输入框共用） */
	UFUNCTION(BlueprintCallable, Category = "Asce|Cheat", meta = (WorldContext = "WorldContextObject"))
	static bool RunFromLine(UObject* WorldContextObject, APlayerController* Requester,
		TSubclassOf<UAsceCheatAction> ActionBase, const FString& Line, FString& OutSummary, FString& OutError);

	/** **仅服务器**：按 `玩家名` 或 `PlayerArray 索引` 开关某个玩家的作弊权限（`asce.grant` 与 Slate 控制台共用） */
	static bool GrantCheat(UWorld* World, const FString& NameOrIndex, bool bAllowed, FString& OutError);

	/** 打开/关闭**本机玩家**的 Slate 控制台（给按键绑定用；也用于 `asce.console`） */
	UFUNCTION(BlueprintCallable, Category = "Asce|Cheat", meta = (WorldContext = "WorldContextObject"))
	static void ToggleConsoleForLocalPlayer(UObject* WorldContextObject);
};
