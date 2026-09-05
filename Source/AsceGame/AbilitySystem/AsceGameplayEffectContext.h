// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GameplayEffectTypes.h"

#include "AsceGameplayEffectContext.generated.h"

class AActor;
class FArchive;
class IAsceAbilitySourceInterface;
class UObject;
class UPhysicalMaterial;

USTRUCT()
struct FAsceGameplayEffectContext : public FGameplayEffectContext
{
	GENERATED_BODY()

	FAsceGameplayEffectContext()
		: FGameplayEffectContext()
	{
	}

	FAsceGameplayEffectContext(AActor* InInstigator, AActor* InEffectCauser)
		: FGameplayEffectContext(InInstigator, InEffectCauser)
	{
	}

	/** Returns the wrapped FAsceGameplayEffectContext from the handle, or nullptr if it doesn't exist or is the wrong type */
	// 通过普通的GameplayEffectHandle获取包裹为AsceGameplayEffectHandle的结构体
	// GameplayEffectContextHandle仅仅是包装的EffectContext的指针
	static ASCEGAME_API FAsceGameplayEffectContext* ExtractEffectContext(struct FGameplayEffectContextHandle Handle);

	/** Sets the object used as the ability source */
	// 设置Source
	void SetAbilitySource(const IAsceAbilitySourceInterface* InObject, float InSourceLevel);

	/** Returns the ability source interface associated with the source object. Only valid on the authority. */
	// 获取能力的Source，仅在权威端有效
	const IAsceAbilitySourceInterface* GetAbilitySource() const;

	virtual FGameplayEffectContext* Duplicate() const override
	{
		FAsceGameplayEffectContext* NewContext = new FAsceGameplayEffectContext();
		*NewContext = *this;
		if (GetHitResult())
		{
			// Does a deep copy of the hit result
			NewContext->AddHitResult(*GetHitResult(), true);
		}
		return NewContext;
	}

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FAsceGameplayEffectContext::StaticStruct();
	}

	/** Overridden to serialize new fields */
	//重载网络序列化函数
	virtual bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) override;

	/** Returns the physical material from the hit result if there is one */
	// 如果HitResult集中了物理材质，那么此函数将返回这个物理材质
	const UPhysicalMaterial* GetPhysicalMaterial() const;

public:
	/** ID to allow the identification of multiple bullets that were part of the same cartridge */
	// 每个能力造成影响时会附带EffectContext，有多个子弹时，使用此ID能快速的按照ID进行分类
	UPROPERTY()
	int32 CartridgeID = -1;

protected:
	
	/** Ability Source object (should implement IAsceAbilitySourceInterface). NOT replicated currently */
	// 和SourceObject大部分时候一致，只不过需要实现IAsceAbilitySourceInterface接口
	UPROPERTY()
	TWeakObjectPtr<const UObject> AbilitySourceObject;
};

template<>
struct TStructOpsTypeTraits<FAsceGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FAsceGameplayEffectContext>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};

