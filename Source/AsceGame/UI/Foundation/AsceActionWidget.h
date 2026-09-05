// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CommonActionWidget.h"
#include "AsceActionWidget.generated.h"

class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;

/** An action widget that will get the icon of key that is currently assigned to the common input action on this widget */
// 获取当前这个控件对应的输入动作(InputAction)的图标并显示
// 这个空间在每个CommonButton上面都有一个
UCLASS(BlueprintType, Blueprintable)
class UAsceActionWidget : public UCommonActionWidget
{
	GENERATED_BODY()

public:

	//~ Begin UCommonActionWidget interface
	virtual FSlateBrush GetIcon() const override;
	//~ End of UCommonActionWidget interface

	/** The Enhanced Input Action that is associated with this Common Input action. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	const TObjectPtr<UInputAction> AssociatedInputAction;

private:

	UEnhancedInputLocalPlayerSubsystem* GetEnhancedInputSubsystem() const;
	
};