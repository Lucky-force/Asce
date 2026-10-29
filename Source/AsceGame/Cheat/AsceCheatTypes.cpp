// Fill out your copyright notice in the Description page of Project Settings.

#include "Cheat/AsceCheatTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceCheatTypes)

// ---------------------------------------------------------------------------------------------
// 基类：默认"把字符串转成值"
// ---------------------------------------------------------------------------------------------

float FAsceCheatArgType::AsFloat(const FString& Content) const
{
	return FCString::Atof(*Content);
}

int32 FAsceCheatArgType::AsInt(const FString& Content) const
{
	return FCString::Atoi(*Content);
}

bool FAsceCheatArgType::AsBool(const FString& Content) const
{
	return Content == TEXT("1")
		|| Content.Equals(TEXT("true"), ESearchCase::IgnoreCase)
		|| Content.Equals(TEXT("yes"), ESearchCase::IgnoreCase);
}

FName FAsceCheatArgType::AsName(const FString& Content) const
{
	return Content.IsEmpty() ? NAME_None : FName(*Content);
}

int64 FAsceCheatArgType::AsEntity(const FString& Content) const
{
	return FCString::Atoi64(*Content);
}

FVector FAsceCheatArgType::AsVector(const FString& Content) const
{
	// 约定 `x,y,z`（控制台按空格切 token ? 参数值内不能有空格）
	TArray<FString> Parts;
	Content.ParseIntoArray(Parts, TEXT(","), /*InCullEmpty*/ true);
	FVector Result = FVector::ZeroVector;
	if (Parts.Num() > 0) { Result.X = FCString::Atof(*Parts[0]); }
	if (Parts.Num() > 1) { Result.Y = FCString::Atof(*Parts[1]); }
	if (Parts.Num() > 2) { Result.Z = FCString::Atof(*Parts[2]); }
	return Result;
}

// ---------------------------------------------------------------------------------------------
// Float / Int
// ---------------------------------------------------------------------------------------------

bool FAsceCheatArgFloat::Validate(const FString& Content, FString& OutError) const
{
	OutError.Reset();
	if (Content.IsEmpty())
	{
		return true;   // 空 = 用默认值 / 必填由 `FAsceCheatActionArg::bRequired` 管
	}
	if (!Content.IsNumeric())
	{
		OutError = TEXT("不是有效数字");
		return false;
	}
	const float Value = FCString::Atof(*Content);
	if (Value < Min || Value > Max)
	{
		OutError = FString::Printf(TEXT("超出范围 [%g, %g]"), Min, Max);
		return false;
	}
	return true;
}

float FAsceCheatArgFloat::AsFloat(const FString& Content) const
{
	return FMath::Clamp(FCString::Atof(*Content), Min, Max);
}

bool FAsceCheatArgInt::Validate(const FString& Content, FString& OutError) const
{
	OutError.Reset();
	if (Content.IsEmpty())
	{
		return true;
	}
	if (!Content.IsNumeric() || Content.Contains(TEXT(".")))
	{
		OutError = TEXT("不是有效整数");
		return false;
	}
	const int32 Value = FCString::Atoi(*Content);
	if (Value < Min || Value > Max)
	{
		OutError = FString::Printf(TEXT("超出范围 [%d, %d]"), Min, Max);
		return false;
	}
	return true;
}

int32 FAsceCheatArgInt::AsInt(const FString& Content) const
{
	return FMath::Clamp(FCString::Atoi(*Content), Min, Max);
}

// ---------------------------------------------------------------------------------------------
// Bool / Entity / Vector
// ---------------------------------------------------------------------------------------------

bool FAsceCheatArgBool::Validate(const FString& Content, FString& OutError) const
{
	OutError.Reset();
	if (Content.IsEmpty())
	{
		return true;
	}
	const bool bKnown = Content == TEXT("1") || Content == TEXT("0")
		|| Content.Equals(TEXT("true"), ESearchCase::IgnoreCase)
		|| Content.Equals(TEXT("false"), ESearchCase::IgnoreCase)
		|| Content.Equals(TEXT("yes"), ESearchCase::IgnoreCase)
		|| Content.Equals(TEXT("no"), ESearchCase::IgnoreCase);
	if (!bKnown)
	{
		OutError = TEXT("只能是 1/0/true/false/yes/no");
		return false;
	}
	return true;
}

bool FAsceCheatArgBool::AsBool(const FString& Content) const
{
	if (Content.Equals(TEXT("false"), ESearchCase::IgnoreCase)
		|| Content.Equals(TEXT("no"), ESearchCase::IgnoreCase)
		|| Content == TEXT("0"))
	{
		return false;
	}
	return FAsceCheatArgType::AsBool(Content);
}

bool FAsceCheatArgEntity::Validate(const FString& Content, FString& OutError) const
{
	OutError.Reset();
	if (Content.IsEmpty())
	{
		return true;
	}
	if (!Content.IsNumeric())
	{
		OutError = TEXT("不是有效实体句柄");
		return false;
	}
	const int64 EntityNumber = FCString::Atoi64(*Content);
	if (EntityNumber <= 0)
	{
		OutError = TEXT("实体句柄必须 > 0");
		return false;
	}
	return true;
}

int64 FAsceCheatArgEntity::AsEntity(const FString& Content) const
{
	return FCString::Atoi64(*Content);
}

bool FAsceCheatArgVector::Validate(const FString& Content, FString& OutError) const
{
	OutError.Reset();
	if (Content.IsEmpty())
	{
		return true;
	}
	TArray<FString> Parts;
	Content.ParseIntoArray(Parts, TEXT(","), /*InCullEmpty*/ true);
	if (Parts.Num() < 1 || Parts.Num() > 3)
	{
		OutError = TEXT("格式应为 x,y,z（逗号分隔，最多 3 个）");
		return false;
	}
	for (const FString& Part : Parts)
	{
		if (!Part.IsNumeric())
		{
			OutError = FString::Printf(TEXT("分量 [%s] 不是有效数字"), *Part);
			return false;
		}
	}
	return true;
}

FVector FAsceCheatArgVector::AsVector(const FString& Content) const
{
	return FAsceCheatArgType::AsVector(Content);
}
