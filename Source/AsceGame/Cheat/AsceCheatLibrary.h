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
	/** 编译期开关（`ASCE_CHEAT_ENABLED`）；关闭时执行/UI 一律不可用 */
	static bool IsCheatSupported();

	/**
	 * cvar `Asce.Cheat.Enable`：**只用于主机/单机启动时给自己授权**（`BeginPlay` 里写 `bCheatAllowed`）。
	 * 不参与服务端鉴权——客户端的实际权限只看组件上复制下去的 `bCheatAllowed`（主机 `asce.grant` 设置）。
	 */
	static bool IsCheatEnabled();

	/** 某模式的全部已注册动作（CDO），按 `分类 + 显示名` 排序 —— UI 枚举用（结果按 ActionBase 缓存） */
	UFUNCTION(BlueprintCallable, Category = "Asce|Cheat")
	static TArray<UAsceCheatAction*> GetActions(TSubclassOf<UAsceCheatAction> ActionBase);

	/** 清动作发现缓存（打开控制台/热更新后调用，避免缓存漏掉新加载的动作类） */
	static void InvalidateActionCache();

	/** 按名字找动作（找不到返回 nullptr） */
	static UAsceCheatAction* FindAction(FName ActionName, TSubclassOf<UAsceCheatAction> ActionBase);

	/** 纯文本动作列表（控制台 `asce.list` 用） */
	UFUNCTION(BlueprintCallable, Category = "Asce|Cheat")
	static FString BuildActionListText(TSubclassOf<UAsceCheatAction> ActionBase);

	/** 执行一个动作（`ProvidedArgs` = 参数名 → 文本内容；没给的使用动作默认值） */
	static bool Run(UObject* WorldContextObject, APlayerController* Requester, UAsceCheatAction* Action,
		const TMap<FName, FString>& ProvidedArgs, FString& OutSummary, FString& OutError,
		bool bHasCursor = false, const FVector& CursorWorldLocation = FVector::ZeroVector);

	/** 解析一整行再执行（控制台 / 命令输入框共用） */
	UFUNCTION(BlueprintCallable, Category = "Asce|Cheat", meta = (WorldContext = "WorldContextObject"))
	static bool RunFromLine(UObject* WorldContextObject, APlayerController* Requester,
		TSubclassOf<UAsceCheatAction> ActionBase, const FString& Line, FString& OutSummary, FString& OutError);

	/** C++ 入口：带请求者本机光标（客户端随 Server RPC 上来；主机本机采集） */
	static bool RunFromLineAt(UObject* WorldContextObject, APlayerController* Requester,
		TSubclassOf<UAsceCheatAction> ActionBase, const FString& Line,
		bool bHasCursor, const FVector& CursorWorldLocation, FString& OutSummary, FString& OutError);

	/** **仅服务器**：按 `玩家名` 或 `PlayerArray 索引` 开关某个玩家的作弊权限（`asce.grant` 与 Slate 控制台共用） */
	static bool GrantCheat(UWorld* World, const FString& NameOrIndex, bool bAllowed, FString& OutError);

	/** 打开/关闭**本机玩家**的 Slate 控制台（给按键绑定用；也用于 `asce.console`） */
	UFUNCTION(BlueprintCallable, Category = "Asce|Cheat", meta = (WorldContext = "WorldContextObject"))
	static void ToggleConsoleForLocalPlayer(UObject* WorldContextObject);
};
