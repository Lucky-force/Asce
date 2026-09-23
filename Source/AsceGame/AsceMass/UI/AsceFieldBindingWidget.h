// AsceFieldBindingWidget.h
#pragma once

#include "CoreMinimal.h"
#include "MassEntityHandle.h"
#include "UI/AsceActivatableWidget.h"
#include "AsceFieldBindingWidget.generated.h"

/**
 * 带"字段绑定标识"的面板基类：**Construct 自动绑定、Destruct 自动解绑**（AS 侧只写 handler + 一行标识）。
 *
 * AS 用法：
 *   UFUNCTION(Meta = (AsceBind = "FAsceHealthFragment:CurrentHealth,FAsceHealthFragment:MaxHealth"))
 *   void OnHealthFieldsChanged(int64 EntityNumber, FName FieldName)
 *   {
 *       // 任一变化：自己用 EntityNumber 去 entity 上重读"生命/最大生命"，重算百分比
 *   }
 *
 * 约定：
 * - handler 签名**必须**是 `(int64 EntityNumber, FName FieldName)`（绑定时校验，不符打 Error 并跳过）；
 * - 标识值 = 逗号分隔的 `FragmentStructName:FieldName`（类型名 = `UScriptStruct` 的名字）；
 * - 绑定目标 = `BindTargetNumbers`（单行列表项设 1 个、多选面板设多个），改完调 `RefreshFieldBindings()`；
 * - `EntityNumber` = `FMassEntityHandle::AsNumber()`（和项目里 AS/BP 的既有约定一致，`FMassEntityHandle` 不是 BlueprintType）。
 *
 * 为什么目标实体不自动取"当前选中"：选中集合属于 UI 策略，面板自己知道该绑谁；这里只提供机制。
 */
UCLASS(Abstract, Blueprintable)
class UAsceFieldBindingWidget : public UAsceActivatableWidget
{
	GENERATED_BODY()

public:
	/** UFunction meta key：绑定标识写在 handler 函数上 */
	static const FName FieldBindMetaKey;

	/** 绑定目标实体（`FMassEntityHandle::AsNumber()`）；AS 设置后调 `RefreshFieldBindings()` */
	UPROPERTY(BlueprintReadWrite, Category = "Asce|FieldBinding")
	TArray<int64> BindTargetNumbers;

	/** 先全部解绑，再按当前 `BindTargetNumbers` + 本类(含父类)的 `AsceBind` 标识重新绑定 */
	UFUNCTION(BlueprintCallable, Category = "Asce|FieldBinding")
	void RefreshFieldBindings();

	/** 解绑本面板的所有字段绑定 */
	UFUNCTION(BlueprintCallable, Category = "Asce|FieldBinding")
	void ClearFieldBindings();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	/** handler 必须是 `(int64, FName)`；不符返回 false 并给出原因 */
	static bool IsHandlerSignatureValid(UFunction* Func, FString& OutError);

	/** 已在绑定的句柄（解绑用） */
	TArray<FDelegateHandle> BoundHandles;
};
