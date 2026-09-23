// AsceFieldBindingWidget.cpp
#include "AsceFieldBindingWidget.h"

#include "AsceMass/UI/AsceFragmentSamplerSubsystem.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

const FName UAsceFieldBindingWidget::FieldBindMetaKey(TEXT("AsceBind"));

void UAsceFieldBindingWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 标识自动绑定（构造即绑）
	RefreshFieldBindings();
}

void UAsceFieldBindingWidget::NativeDestruct()
{
	// 销毁即解绑
	ClearFieldBindings();

	Super::NativeDestruct();
}

void UAsceFieldBindingWidget::ClearFieldBindings()
{
	if (UAsceFragmentSamplerSubsystem* SamplerSubsystem = UAsceFragmentSamplerSubsystem::Get(this))
	{
		for (const FDelegateHandle& Handle : BoundHandles)
		{
			SamplerSubsystem->UnbindField(Handle);
		}
	}
	BoundHandles.Reset();
}

void UAsceFieldBindingWidget::RefreshFieldBindings()
{
	ClearFieldBindings();

	if (BindTargetNumbers.Num() == 0)
	{
		return;
	}

	UAsceFragmentSamplerSubsystem* SamplerSubsystem = UAsceFragmentSamplerSubsystem::Get(this);
	if (!SamplerSubsystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AsceFieldBinding] %s 找不到 UAsceFragmentSamplerSubsystem"), *GetName());
		return;
	}

	// 遍历本类（含父类）所有 UFUNCTION，找带 `AsceBind` 标识的 handler
	for (TFieldIterator<UFunction> It(GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		UFunction* Func = *It;
		if (!Func)
		{
			continue;
		}

		const FString MetaValue = Func->GetMetaData(FieldBindMetaKey);
		if (MetaValue.IsEmpty())
		{
			continue;
		}

		FString SignatureError;
		if (!IsHandlerSignatureValid(Func, SignatureError))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[AsceFieldBinding] %s::%s 的 AsceBind 标识无法生效：%s（应为 (int64 EntityNumber, FName FieldName)）"),
				*GetName(), *Func->GetName(), *SignatureError);
			continue;
		}

		TArray<FString> Entries;
		MetaValue.ParseIntoArray(Entries, TEXT(","), /*CullEmpty=*/true);
		for (const FString& RawEntry : Entries)
		{
			FString TypeName;
			FString FieldName;
			if (!RawEntry.TrimStartAndEnd().Split(TEXT(":"), &TypeName, &FieldName) || FieldName.IsEmpty())
			{
				UE_LOG(LogTemp, Error,
					TEXT("[AsceFieldBinding] %s::%s 的标识项格式不对（应为 \"FXXXFragment:FieldName\"）：%s"),
					*GetName(), *Func->GetName(), *RawEntry);
				continue;
			}

			const UScriptStruct* FragmentType = SamplerSubsystem->FindFragmentTypeByName(FName(*TypeName));
			if (!FragmentType)
			{
				UE_LOG(LogTemp, Error, TEXT("[AsceFieldBinding] %s::%s 的标识项找不到 fragment 类型：%s"),
					*GetName(), *Func->GetName(), *TypeName);
				continue;
			}

			// 普通 TDelegate 直接调 UFUNCTION（AS/BP 的 handler 都能这样接；不需要 dynamic delegate）
			const FAsceFragmentFieldChangedDelegate Delegate =
				FAsceFragmentFieldChangedDelegate::CreateUFunction(this, Func->GetFName());

			for (const int64 EntityNumber : BindTargetNumbers)
			{
				const FMassEntityHandle Entity = FMassEntityHandle::FromNumber(static_cast<uint64>(EntityNumber));
				const FDelegateHandle Handle = SamplerSubsystem->BindField(Entity, FragmentType, FName(*FieldName), Delegate);
				if (Handle.IsValid())
				{
					BoundHandles.Add(Handle);
				}
			}
		}
	}
}

bool UAsceFieldBindingWidget::IsHandlerSignatureValid(UFunction* Func, FString& OutError)
{
	if (!Func)
	{
		OutError = TEXT("UFunction 为空");
		return false;
	}

	// 参数按声明顺序（有返回值时 Return 属性也在 Parm 里，排在第一个 ⇒ 这里要求恰好两个参数即可）
	TArray<const FProperty*> Parms;
	for (TFieldIterator<FProperty> It(Func); It; ++It)
	{
		if (It->HasAnyPropertyFlags(CPF_Parm))
		{
			Parms.Add(*It);
		}
	}

	if (Parms.Num() != 2)
	{
		OutError = FString::Printf(TEXT("参数个数为 %d"), Parms.Num());
		return false;
	}

	const bool bFirstOk = (CastField<FInt64Property>(Parms[0]) != nullptr);
	const bool bSecondOk = (CastField<FNameProperty>(Parms[1]) != nullptr);
	if (!bFirstOk || !bSecondOk)
	{
		OutError = TEXT("参数类型不匹配");
		return false;
	}
	return true;
}
