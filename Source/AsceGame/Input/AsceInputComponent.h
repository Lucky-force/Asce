#pragma once

#include "EnhancedInputComponent.h"
#include "AsceInputConfig.h"

#include "AsceInputComponent.generated.h"

class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UObject;


/**
 * UAsceInputComponent
 *
 *	Component used to manage input mappings and bindings using an input config data asset.
 *	继承自增强输入组件，在此基础上添加了从InputConfig中取出输入数据来进行设置的功能
 */
UCLASS(Config = Input)
class UAsceInputComponent : public UEnhancedInputComponent
{
	GENERATED_BODY()

public:

	UAsceInputComponent(const FObjectInitializer& ObjectInitializer);

	void AddInputMappings(const UAsceInputConfig* InputConfig, UEnhancedInputLocalPlayerSubsystem* InputSubsystem) const;
	void RemoveInputMappings(const UAsceInputConfig* InputConfig, UEnhancedInputLocalPlayerSubsystem* InputSubsystem) const;

	/**
	 * @param InputConfig IA所在的InputConfig
	 * @param InputTag IA的GameplayTag
	 * @param TriggerEvent IA的触发类型
	 * @param Object 绑定到的对象指针
	 * @param Func 触发时的回调函数（可以为UFUNCTION的名字）
	 * @param bLogIfNotFound 是否在没有找到要绑定的IA时打印日志
	 **/
	template<class UserClass, typename FuncType>
	void BindNativeAction(const UAsceInputConfig* InputConfig, const FGameplayTag& InputTag, ETriggerEvent TriggerEvent, UserClass* Object, FuncType Func, bool bLogIfNotFound);

	/**
	 * @param InputConfig 要绑定的Ability IA所在的InputConfig
	 * @param Object 绑定到的对象指针
	 * @param PressedFunc 按键按下时调用的回调函数
	 * @param ReleasedFunc 按键释放时调用的回调函数
	 * @param BindHandles 返回绑定的Handles
	 ***/
	template<class UserClass, typename PressedFuncType, typename ReleasedFuncType>
	void BindAbilityActions(const UAsceInputConfig* InputConfig, UserClass* Object, PressedFuncType PressedFunc, ReleasedFuncType ReleasedFunc, TArray<uint32>& BindHandles);

	void RemoveBinds(TArray<uint32>& BindHandles);
};


template<class UserClass, typename FuncType>
void UAsceInputComponent::BindNativeAction(const UAsceInputConfig* InputConfig, const FGameplayTag& InputTag, ETriggerEvent TriggerEvent, UserClass* Object, FuncType Func, bool bLogIfNotFound)
{
	check(InputConfig);
	if (const UInputAction* IA = InputConfig->FindNativeInputActionForTag(InputTag, bLogIfNotFound))
	{
		BindAction(IA, TriggerEvent, Object, Func);
	}
}

template<class UserClass, typename PressedFuncType, typename ReleasedFuncType>
void UAsceInputComponent::BindAbilityActions(const UAsceInputConfig* InputConfig, UserClass* Object, PressedFuncType PressedFunc, ReleasedFuncType ReleasedFunc, TArray<uint32>& BindHandles)
{
	check(InputConfig);

	for (const FAsceInputAction& Action : InputConfig->AbilityInputActions)
	{
		if (Action.InputAction && Action.InputTag.IsValid())
		{
			if (PressedFunc)
			{
				BindHandles.Add(BindAction(Action.InputAction, ETriggerEvent::Triggered, Object, PressedFunc, Action.InputTag).GetHandle());
			}

			if (ReleasedFunc)
			{
				BindHandles.Add(BindAction(Action.InputAction, ETriggerEvent::Completed, Object, ReleasedFunc, Action.InputTag).GetHandle());
			}
		}
	}
}
