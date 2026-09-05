// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Templates/SharedPointer.h"

/**
 * 
 */
class ISlateStyle;

class FGameEditorStyle
{
	
public:

	static void Initialize();

	static void Shutdown();
	
	static const ISlateStyle& Get();

	static FName GetStyleSetName();

private:

	static TSharedRef< class FSlateStyleSet > Create();

private:
	static TSharedPtr< class FSlateStyleSet > StyleInstance;
};
