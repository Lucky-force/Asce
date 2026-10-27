// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Object.h"

#include "AsceCheatTypes.generated.h"

class APlayerController;
class UWorld;
class UAsceCheatAction;

/**
 * 作弊参数的**类型对象**：自己知道怎么校验 / 给候选 / 取值。
 *
 * 用 `FInstancedStruct` 存（和 `FAsceSkillCastConfig` 同一套路）⇒ **加一种参数 = 加一个 USTRUCT**。
 * 通用类型在本文件；模式专用类型（实体行 / 动作行 / 阵营）在各模式目录，如 `Actions/RTS/AsceRTSCheatArgTypes.h`。
 */
USTRUCT()
struct ASCEGAME_API FAsceCheatArgType
{
	GENERATED_BODY()

	virtual ~FAsceCheatArgType() = default;

	/** UI 用：控件类型名（Float / Int / Bool / Name / Vector / Entity …） */
	virtual FString GetTypeName() const { return TEXT("Name"); }

	/** 校验；失败时写 OutError，返回 false */
	virtual bool Validate(const FString& Content, FString& OutError) const
	{
		OutError.Reset();
		return true;
	}

	/** 候选值 —— UI 下拉框；空数组 = 自由输入 */
	virtual TArray<FString> GetSuggestions(const UWorld* World) const { return {}; }

	// 取值（内容非法/类型不符时给安全默认值）
	virtual float   AsFloat (const FString& Content) const;
	virtual int32   AsInt   (const FString& Content) const;
	virtual bool    AsBool  (const FString& Content) const;
	virtual FName   AsName  (const FString& Content) const;
	virtual int64   AsEntity(const FString& Content) const;
	virtual FVector AsVector(const FString& Content) const;
};

/** 浮点（可带 Min/Max，UI 可渲染成滑条） */
USTRUCT()
struct ASCEGAME_API FAsceCheatArgFloat : public FAsceCheatArgType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Cheat")
	float Min = -FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Cheat")
	float Max = FLT_MAX;

	virtual FString GetTypeName() const override { return TEXT("Float"); }
	virtual bool Validate(const FString& Content, FString& OutError) const override;
	virtual float AsFloat(const FString& Content) const override;
};

/** 整数（同上） */
USTRUCT()
struct ASCEGAME_API FAsceCheatArgInt : public FAsceCheatArgType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Cheat")
	int32 Min = MIN_int32;

	UPROPERTY(EditAnywhere, Category = "Cheat")
	int32 Max = MAX_int32;

	virtual FString GetTypeName() const override { return TEXT("Int"); }
	virtual bool Validate(const FString& Content, FString& OutError) const override;
	virtual int32 AsInt(const FString& Content) const override;
};

/** 布尔（`1/0/true/false/yes/no`，UI 渲染成勾选框） */
USTRUCT()
struct ASCEGAME_API FAsceCheatArgBool : public FAsceCheatArgType
{
	GENERATED_BODY()

	virtual FString GetTypeName() const override { return TEXT("Bool"); }
	virtual bool Validate(const FString& Content, FString& OutError) const override;
	virtual bool AsBool(const FString& Content) const override;
};

/** 名字（FName，UI 文本框） */
USTRUCT()
struct ASCEGAME_API FAsceCheatArgName : public FAsceCheatArgType
{
	GENERATED_BODY()

	virtual FString GetTypeName() const override { return TEXT("Name"); }
};

/** 实体句柄（int64：`FMassEntityHandle::AsNumber()`），UI 可带"取当前选中"按钮 */
USTRUCT()
struct ASCEGAME_API FAsceCheatArgEntity : public FAsceCheatArgType
{
	GENERATED_BODY()

	virtual FString GetTypeName() const override { return TEXT("Entity"); }
	virtual bool Validate(const FString& Content, FString& OutError) const override;
	virtual int64 AsEntity(const FString& Content) const override;
};

/** 三维向量（内容写 `x,y,z`，UI 可渲染成三个输入框） */
USTRUCT()
struct ASCEGAME_API FAsceCheatArgVector : public FAsceCheatArgType
{
	GENERATED_BODY()

	virtual FString GetTypeName() const override { return TEXT("Vector"); }
	virtual bool Validate(const FString& Content, FString& OutError) const override;
	virtual FVector AsVector(const FString& Content) const override;
};

/**
 * 一个动作参数的**一条定义**：名字 + 内容（唯一真相）+ 类型对象 + UI 提示。
 *
 * 内容统一是字符串（控制台和 UI 都写它），`GetFloat()/GetName()/...` 内部转成实际值。
 */
USTRUCT()
struct ASCEGAME_API FAsceCheatActionArg
{
	GENERATED_BODY()

	UPROPERTY()
	FName ArgName;

	/** 参数内容（文本；默认值也存这里；执行前会被输入覆盖） */
	UPROPERTY()
	FString ArgContent;

	/** `FAsceCheatArgType` 的实例（类型 + 该参数配置） */
	UPROPERTY()
	FInstancedStruct ArgType;

	/** UI 显示名 / 提示 */
	UPROPERTY()
	FText DisplayName;

	UPROPERTY()
	FText Tooltip;

	/** 必填（留空 ? 执行前报错） */
	UPROPERTY()
	bool bRequired = false;

	const FAsceCheatArgType* GetArgType() const { return ArgType.GetPtr<FAsceCheatArgType>(); }

	float   GetFloat () const { const FAsceCheatArgType* T = GetArgType(); return T ? T->AsFloat (ArgContent) : 0.f; }
	int32   GetInt   () const { const FAsceCheatArgType* T = GetArgType(); return T ? T->AsInt   (ArgContent) : 0; }
	bool    GetBool  () const { const FAsceCheatArgType* T = GetArgType(); return T ? T->AsBool  (ArgContent) : false; }
	FName   GetName  () const { const FAsceCheatArgType* T = GetArgType(); return T ? T->AsName  (ArgContent) : NAME_None; }
	int64   GetEntity() const { const FAsceCheatArgType* T = GetArgType(); return T ? T->AsEntity(ArgContent) : 0; }
	FVector GetVector() const { const FAsceCheatArgType* T = GetArgType(); return T ? T->AsVector(ArgContent) : FVector::ZeroVector; }
};

/** 执行上下文（C++ 只有，不暴露给蓝图） */
struct FAsceCheatContext
{
	UWorld* World = nullptr;
	APlayerController* Requester = nullptr;

	/** 请求者的阵营（`AAscePlayerState::GetTeamId()`；取不到 = 0） */
	int32 RequesterTeamId = 0;
};

namespace AsceCheat
{
	/** 构造一个参数定义（动作 `BuildArgs()` 里用） */
	template<typename TArgType>
	FAsceCheatActionArg MakeArg(const FName Name, const FText& InDisplayName, const FString& DefaultContent = FString()
		, const bool bInRequired = false)
	{
		FAsceCheatActionArg Arg;
		Arg.ArgName = Name;
		Arg.ArgContent = DefaultContent;
		Arg.DisplayName = InDisplayName;
		Arg.bRequired = bInRequired;
		Arg.ArgType.InitializeAs<TArgType>();
		return Arg;
	}
}
