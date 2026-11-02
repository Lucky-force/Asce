// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/SoftObjectPtr.h"

#include "AsceUnitModule.generated.h"

class UAnimSequence;
struct FAsceRTSUnitDef;
class UMassEntityConfigAsset;

/**
 * 构建上下文：让模块能读到**其它模块的参数**（如 Vision 读 Volume 的 Height），
 * 但这只是"数据可见"，**不构成模块依赖**。
 */
struct FAsceUnitModuleBuildContext
{
	const TArray<FInstancedStruct>* Modules = nullptr;

	template<typename TModule>
	const TModule* Find() const
	{
		if (Modules)
		{
			for (const FInstancedStruct& Module : *Modules)
			{
				if (const TModule* Typed = Module.GetPtr<TModule>())
				{
					return Typed;
				}
			}
		}
		return nullptr;
	}
};

/**
 * **模块预览的"积木盒"**：基础层只提供能力（动画条目 / 圆 / 胶囊 / 盒），
 * 每个模块在自己 `CollectPreview()` 里决定"我要预览什么"。
 * 面板把所有模块的产物汇总后交给预览视口，不写死任何具体状态。
 */
struct FAsceUnitModulePreviewActions
{
	struct FAnimationEntry
	{
		FString Label;
		TSoftObjectPtr<UAnimSequence> Sequence;
	};

	struct FCirclePreview
	{
		float RadiusCm = 0.f;
		FLinearColor Color = FLinearColor::White;
		float Z = 0.f;
	};

	struct FCapsulePreview
	{
		float RadiusCm = 0.f;
		float HalfHeightCm = 0.f;
		FLinearColor Color = FLinearColor::White;
	};

	struct FBoxPreview
	{
		FVector ExtentCm = FVector::ZeroVector;
		FLinearColor Color = FLinearColor::White;
	};

	TArray<FAnimationEntry> Animations;
	TArray<FCirclePreview> Circles;
	TArray<FCapsulePreview> Capsules;
	TArray<FBoxPreview> Boxes;

	void AddAnimation(const FString& Label, const TSoftObjectPtr<UAnimSequence>& Sequence)
	{
		if (!Sequence.IsNull())
		{
			Animations.Add({ Label, Sequence });
		}
	}

	void AddCircle(const float RadiusCm, const FLinearColor& Color, const float Z = 0.f)
	{
		if (RadiusCm > 0.f)
		{
			Circles.Add({ RadiusCm, Color, Z });
		}
	}

	void AddCapsule(const float RadiusCm, const float HalfHeightCm, const FLinearColor& Color)
	{
		if (RadiusCm > 0.f)
		{
			Capsules.Add({ RadiusCm, HalfHeightCm, Color });
		}
	}

	void AddBox(const FVector& ExtentCm, const FLinearColor& Color)
	{
		if (!ExtentCm.IsNearlyZero())
		{
			Boxes.Add({ ExtentCm, Color });
		}
	}
};

namespace AsceUnitModuleUtil
{
	template<typename TFragment>
	TFragment& AddFragment(TArray<FInstancedStruct>& OutFragments)
	{
		return OutFragments.Add_GetRef(FInstancedStruct::Make<TFragment>()).GetMutable<TFragment>();
	}
}

/**
 * **单位模块基类**：纯数据在派生结构里，行为用虚函数（都在编辑器侧）。
 *
 * 运行时看不到模块：运行时只有"fragment（含初值）+ def 引用"。
 * 每个模块一个 .h/.cpp（`AsceUnitModuleXxx.*`），便于各自扩展预览/校验逻辑。
 *
 * 见 `AsceMass/Data/单位模块化设计.md` §5。
 */
USTRUCT()
struct FAsceUnitModule
{
	GENERATED_BODY()

	virtual ~FAsceUnitModule() = default;

	/** 唯一 id（面板/资产里按它认模块） */
	virtual FName GetModuleId() const PURE_VIRTUAL(FAsceUnitModule::GetModuleId, return NAME_None;);

	virtual FText GetDisplayName() const { return FText::FromName(GetModuleId()); }

	virtual FText GetCategory() const { return FText::GetEmpty(); }

	/** 模块参数 → fragment / tag（带初值）；需要别的 fragment 存在就自己加，并集会自动去重 */
	virtual void Contribute(const FAsceUnitModuleBuildContext& Context
		, TArray<FInstancedStruct>& OutFragments, TArray<const UScriptStruct*>& OutTags) const {}

	/** 模块参数 → def 的非 fragment 字段（`AnimSet` / `AttackAction` / `SkillSlots`） */
	virtual void WriteDefRow(FAsceRTSUnitDef& Row) const {}

	/** 模块写入 EntityTemplate 上的 trait（例如 Visualization 写 MassMovableVisualizationTrait） */
	virtual void WriteToTemplate(UMassEntityConfigAsset& ConfigAsset) const {}

	/** 保存/编辑期校验 */
	virtual void Validate(TArray<FText>& OutErrors) const {}

	/** 编辑器预览：把"要展示的动画 / 几何"塞进 Actions（基础层负责渲染） */
	virtual void CollectPreview(FAsceUnitModulePreviewActions& Actions) const {}
};
