// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CommonButtonBase.h"

#include "AsceButtonBase.generated.h"

class UObject;
struct FFrame;

//按钮空间抽象基类
UCLASS(Abstract, BlueprintType, Blueprintable)
class UAsceButtonBase : public UCommonButtonBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	void SetButtonText(const FText& InText);
	
protected:
	// UUserWidget interface
	virtual void NativePreConstruct() override;
	// End of UUserWidget interface

	// UCommonButtonBase interface
	virtual void UpdateInputActionWidget() override;
	virtual void OnInputMethodChanged(ECommonInputType CurrentInputType) override;
	// End of UCommonButtonBase interface

	void RefreshButtonText();
	
	//控件蓝图重载，真正刷新ButtonText的地方
	UFUNCTION(BlueprintImplementableEvent)
	void UpdateButtonText(const FText& InText);

	//控件蓝图重载，真正刷新按钮样式的地方
	UFUNCTION(BlueprintImplementableEvent)
	void UpdateButtonStyle();
	
private:
	
	//手动设置的按钮文本不为空时这个值为True
	UPROPERTY(EditAnywhere, Category="Button", meta=(InlineEditConditionToggle))
	uint8 bOverride_ButtonText : 1;
	
	UPROPERTY(EditAnywhere, Category="Button", meta=( editcondition="bOverride_ButtonText" ))
	FText ButtonText;
};
