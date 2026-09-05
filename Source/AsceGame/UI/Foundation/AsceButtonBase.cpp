// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceButtonBase.h"
#include "CommonActionWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceButtonBase)

void UAsceButtonBase::NativePreConstruct()
{
	//控件编辑器里面同步刷新
	Super::NativePreConstruct();

	UpdateButtonStyle();
	RefreshButtonText();
}

void UAsceButtonBase::UpdateInputActionWidget()
{
	Super::UpdateInputActionWidget();

	UpdateButtonStyle();
	RefreshButtonText();
}

void UAsceButtonBase::SetButtonText(const FText& InText)
{
	bOverride_ButtonText = InText.IsEmpty();
	ButtonText = InText;
	RefreshButtonText();
}

void UAsceButtonBase::RefreshButtonText()
{
	//如果按钮没有传入文本，那么用这个inputactionwidget上面的绑定的默认文本
	if (bOverride_ButtonText || ButtonText.IsEmpty())
	{
		if (InputActionWidget)
		{
			const FText ActionDisplayText = InputActionWidget->GetDisplayText();	
			if (!ActionDisplayText.IsEmpty())
			{
				UpdateButtonText(ActionDisplayText);
				return;
			}
		}
	}
	
	UpdateButtonText(ButtonText);	
}

void UAsceButtonBase::OnInputMethodChanged(ECommonInputType CurrentInputType)
{
	Super::OnInputMethodChanged(CurrentInputType);

	UpdateButtonStyle();
}
