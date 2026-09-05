// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "EditorValidatorBase.h"
#include "EditorValidator.generated.h"

/**
 * 
 */


class UObject;


//输出设备，shipping包中不方便打印日志，因此可以继承此类，在序列化函数中对日志进行处理, 随后将日志输出到某个文件中
//总的来说，就是在运行时产生的日志会放到定义的两个字符串数组中，随后被写入某个日志文件，这样就能在包体中查看日志了
class FAsceValidationMessageGatherer : public FOutputDevice
{
public:
	FAsceValidationMessageGatherer()
		: FOutputDevice()
	{
		GLog->AddOutputDevice(this);
	}

	virtual ~FAsceValidationMessageGatherer()
	{
		GLog->RemoveOutputDevice(this);
	}

	//Params
	//@V 日志内容
	//@Verbosity 日志级别
	//@Category 日志类别目录
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		if (Verbosity <= ELogVerbosity::Warning)
		{
			FString MessageString(V);
			bool bIgnored = false;
			for (const FString& IgnorePattern : IgnorePatterns)
			{
				if (MessageString.Contains(IgnorePattern))
				{
					bIgnored = true;
					break;
				}
			}

			if (!bIgnored)
			{
				AllWarningsAndErrors.Add(MessageString);
				if (Verbosity == ELogVerbosity::Warning)
				{
					AllWarnings.Add(MessageString);
				}
			}
		}
	}

	const TArray<FString>& GetAllWarningsAndErrors() const
	{
		return AllWarningsAndErrors;
	}

	const TArray<FString>& GetAllWarnings() const
	{
		return AllWarnings;
	}

	static void AddIgnorePatterns(const TArray<FString>& NewPatterns)
	{
		IgnorePatterns.Append(NewPatterns);
	}

	static void RemoveIgnorePatterns(const TArray<FString>& PatternsToRemove)
	{
		for (const FString& PatternToRemove : PatternsToRemove)
		{
			IgnorePatterns.RemoveSingleSwap(PatternToRemove);
		}
	}

private:
	TArray<FString> AllWarningsAndErrors;
	TArray<FString> AllWarnings;
	
	//日志中有这些部分的字符串且日志级别小于等于warning就会被忽略
	static TArray<FString> IgnorePatterns;
};


UCLASS(Abstract)
class UEditorValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	UEditorValidator();

	static void ValidateCheckedOutContent(bool bInteractive, const EDataValidationUsecase InValidationUsecase);
	static bool ValidatePackages(const TArray<FString>& ExistingPackageNames, const TArray<FString>& DeletedPackageNames, int32 MaxPackagesToLoad, TArray<FString>& OutAllWarningsAndErrors, const EDataValidationUsecase InValidationUsecase);
	static bool ValidateProjectSettings();

	static bool IsInUncookedFolder(const FString& PackageName, FString* OutUncookedFolderName = nullptr);
	static bool ShouldAllowFullValidation();

	static void GetChangedAssetsForCode(class IAssetRegistry& AssetRegistry, const FString& ChangedHeaderLocalFilename, TArray<FString>& OutChangedPackageNames);

protected:
	virtual bool CanValidateAsset_Implementation(UObject* InAsset) const override;

	static TArray<FString> TestMapsFolders;

private:
	/**
	 * Used by some validators to determine if it is okay to load referencing assets or other slow tasks. 
	 * This is not okay for fast operations like saving, but is fine for slower "check everything thoroughly" tests
	 * 详细且缓慢额检查资产引用加载或者其他任务是否合法，不要在需要快速操作的地方，比如保存时使用。
	 */
	static bool bAllowFullValidationInEditor;
};