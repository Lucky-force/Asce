#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "AsceInputConfig.generated.h"

class UInputAction;
class UObject;
struct FFrame;

/**
 * FAsceInputAction
 *
 *	Struct used to map a input action to a gameplay input tag.
 */
USTRUCT(BlueprintType)
struct FAsceInputAction
{
	GENERATED_BODY()

public:

	//IA
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<const UInputAction> InputAction = nullptr;
	
	//IA对应的Tag
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Meta = (Categories = "InputTag"))
	FGameplayTag InputTag;
};

/**
 * UAsceInputConfig
 *
 *	Non-mutable data asset that contains input configuration properties.
 */
UCLASS(BlueprintType, Const)
class UAsceInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	UAsceInputConfig(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "Asce|Pawn")
	const UInputAction* FindNativeInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound = true) const;

	UFUNCTION(BlueprintCallable, Category = "Asce|Pawn")
	const UInputAction* FindAbilityInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound = true) const;

public:
	// List of input actions used by the owner.  These input actions are mapped to a gameplay tag and must be manually bound.
	// 所有的NativeInputActions，必须手动绑定，和能力无关（比如走路，移动视角这种输入）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Meta = (TitleProperty = "InputAction"))
	TArray<FAsceInputAction> NativeInputActions;

	// List of input actions used by the owner.  These input actions are mapped to a gameplay tag and are automatically bound to abilities with matching input tags.
	// 只需要调用InputComponent的初始化绑定，就会自动绑定AbilityInputActions中的IA到ASC中，当对应按键触发时，将传入按键状态到ASC中进行技能激活处理
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Meta = (TitleProperty = "InputAction"))
	TArray<FAsceInputAction> AbilityInputActions;
};
