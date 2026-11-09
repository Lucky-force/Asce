// AsceUIThemeSettings.cpp
#include "AsceUIThemeSettings.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

#include "Templates/SubclassOf.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceUIThemeSettings)

UAsceUIStyleSet::UAsceUIStyleSet()
{
	// 默认间距（可在项目设置里改）
	SpaceScale.Add(EAsceUISpace::None, 0.f);
	SpaceScale.Add(EAsceUISpace::XS, 2.f);
	SpaceScale.Add(EAsceUISpace::S, 4.f);
	SpaceScale.Add(EAsceUISpace::M, 8.f);
	SpaceScale.Add(EAsceUISpace::L, 12.f);
	SpaceScale.Add(EAsceUISpace::XL, 20.f);
}


UClass* UAsceUIStyleSet::FindPanelClass(const EAsceUIPanelStyleToken Token) const
{
	if (Token == EAsceUIPanelStyleToken::None)
	{
		return nullptr;
	}
	const TSoftClassPtr<UBorder>* Found = PanelStyles.Find(Token);
	return Found ? Found->LoadSynchronous() : nullptr;
}

UClass* UAsceUIStyleSet::FindImageClass(const EAsceUIImageStyleToken Token) const
{
	if (Token == EAsceUIImageStyleToken::None)
	{
		return nullptr;
	}
	const TSoftClassPtr<UImage>* Found = ImageStyles.Find(Token);
	return Found ? Found->LoadSynchronous() : nullptr;
}

UClass* UAsceUIStyleSet::FindTextClass(const EAsceUITextStyleToken Token) const
{
	if (Token == EAsceUITextStyleToken::None)
	{
		return nullptr;
	}
	const TSoftClassPtr<UTextBlock>* Found = TextStyles.Find(Token);
	return Found ? Found->LoadSynchronous() : nullptr;
}

UClass* UAsceUIStyleSet::FindButtonClass(const EAsceUIButtonStyleToken Token) const
{
	if (Token == EAsceUIButtonStyleToken::None)
	{
		return nullptr;
	}
	const TSoftClassPtr<UWidget>* Found = ButtonStyles.Find(Token);
	return Found ? Found->LoadSynchronous() : nullptr;
}

UClass* UAsceUIStyleSet::FindBarClass(const EAsceUIBarStyleToken Token) const
{
	if (Token == EAsceUIBarStyleToken::None)
	{
		return nullptr;
	}
	const TSoftClassPtr<UProgressBar>* Found = BarStyles.Find(Token);
	return Found ? Found->LoadSynchronous() : nullptr;
}

float UAsceUIStyleSet::GetSpace(const EAsceUISpace Space) const
{
	if (const float* Value = SpaceScale.Find(Space))
	{
		return *Value;
	}
	return 0.f;
}

FMargin UAsceUIStyleSet::GetMargin(const EAsceUISpace Space) const
{
	const float Value = GetSpace(Space);
	return FMargin(Value);
}
