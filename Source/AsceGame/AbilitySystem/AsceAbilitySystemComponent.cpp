// Fill out your copyright notice in the Description page of Project Settings.


#include "AsceAbilitySystemComponent.h"

#include "AbilitySystem/Abilities/AsceGameplayAbility.h"
#include "AbilitySystem/AsceAbilityTagRelationshipMapping.h"
#include "Animation/AsceAnimInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "AsceGlobalAbilitySystem.h"
#include "AsceLogChannels.h"
#include "System/AsceAssetManager.h"
#include "System/AsceGameData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceAbilitySystemComponent)

UE_DEFINE_GAMEPLAY_TAG(TAG_Gameplay_AbilityInputBlocked, "Gameplay.AbilityInputBlocked");

UAsceAbilitySystemComponent::UAsceAbilitySystemComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	InputHeldSpecHandles.Reset();

	//FMemory::Memset(ActivationGroupCounts, 0, sizeof(ActivationGroupCounts));
}

void UAsceAbilitySystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAsceGlobalAbilitySystem* GlobalAbilitySystem = UWorld::GetSubsystem<UAsceGlobalAbilitySystem>(GetWorld()))
	{
		GlobalAbilitySystem->UnregisterASC(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UAsceAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	//初始化ActorInfo, 此时传入的Owner 和 Avatar, 用来初始化ActorInfo中的数据
	FGameplayAbilityActorInfo* ActorInfo = AbilityActorInfo.Get();
	check(ActorInfo);
	check(InOwnerActor);

	const bool bHasNewPawnAvatar = Cast<APawn>(InAvatarActor) && (InAvatarActor != ActorInfo->AvatarActor);

	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);

	if (bHasNewPawnAvatar)
	{
		// Notify all abilities that a new pawn avatar has been set
		// 通知所有能力一个新的avatar被设置了
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			ensureMsgf(AbilitySpec.Ability && AbilitySpec.Ability->GetInstancingPolicy() != EGameplayAbilityInstancingPolicy::NonInstanced, TEXT("InitAbilityActorInfo: All Abilities should be Instanced (NonInstanced is being deprecated due to usability issues)."));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	
			TArray<UGameplayAbility*> Instances = AbilitySpec.GetAbilityInstances();
			for (UGameplayAbility* AbilityInstance : Instances)
			{
				UAsceGameplayAbility* AsceAbilityInstance = Cast<UAsceGameplayAbility>(AbilityInstance);
				if (AsceAbilityInstance)
				{
					// Ability instances may be missing for replays
					AsceAbilityInstance->OnPawnAvatarSet();
				}
			}
		}

		// Register with the global system once we actually have a pawn avatar. We wait until this time since some globally-applied effects may require an avatar.
		// 一旦我们有一个pawn avatar就到全局能力系统中注册一下，这样在全局能力系统应用一些全局的effect时，可以保证当前查询到的已注册能力组件的avatar都存在
		if (UAsceGlobalAbilitySystem* GlobalAbilitySystem = UWorld::GetSubsystem<UAsceGlobalAbilitySystem>(GetWorld()))
		{
			GlobalAbilitySystem->RegisterASC(this);
		}

		// 初始化动画实例
		if (UAsceAnimInstance* AsceAnimInst = Cast<UAsceAnimInstance>(ActorInfo->GetAnimInstance()))
		{
			AsceAnimInst->InitializeWithAbilitySystem(this);
		}
		
		// 尝试激活生成avatar时便激活的能力
		TryActivateAbilitiesOnSpawn();
	}
}

void UAsceAbilitySystemComponent::TryActivateAbilitiesOnSpawn()
{
	ABILITYLIST_SCOPE_LOCK();
	//非实例化的能力和每次执行时生成实例的能力在ActivitableAbilities中是存的cdo指针，而actor instance的能力则是真的实例
	for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (const UAsceGameplayAbility* AsceAbilityCDO = Cast<UAsceGameplayAbility>(AbilitySpec.Ability))
		{
			AsceAbilityCDO->TryActivateAbilityOnSpawn(AbilityActorInfo.Get(), AbilitySpec);
		}
	}
}

void UAsceAbilitySystemComponent::CancelAbilitiesByFunc(TShouldCancelAbilityFunc ShouldCancelFunc, bool bReplicateCancelAbility)
{
	
	//此函数取消能通过函数ShouldCancelFunc判断的所有能力（这些能力必须是可取消的）
	
	ABILITYLIST_SCOPE_LOCK();
	//能力列表作用域锁，防止在遍历能力的时候调用此函数停止或者取消某个能力导致ActivatableAbilities的内存发生变化
	for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (!AbilitySpec.IsActive())
		{
			continue;
		}

		UAsceGameplayAbility* AsceAbilityCDO = Cast<UAsceGameplayAbility>(AbilitySpec.Ability);
		if (!AsceAbilityCDO)
		{
			UE_LOG(LogAsceAbilitySystem, Error, TEXT("CancelAbilitiesByFunc: Non-AsceGameplayAbility %s was Granted to ASC. Skipping."), *AbilitySpec.Ability.GetName());
			continue;
		}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
		ensureMsgf(AbilitySpec.Ability->GetInstancingPolicy() != EGameplayAbilityInstancingPolicy::NonInstanced, TEXT("CancelAbilitiesByFunc: All Abilities should be Instanced (NonInstanced is being deprecated due to usability issues)."));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			
		// Cancel all the spawned instances.
		TArray<UGameplayAbility*> Instances = AbilitySpec.GetAbilityInstances();
		for (UGameplayAbility* AbilityInstance : Instances)
		{
			UAsceGameplayAbility* AsceAbilityInstance = CastChecked<UAsceGameplayAbility>(AbilityInstance);

			if (ShouldCancelFunc(AsceAbilityInstance, AbilitySpec.Handle))
			{
				if (AsceAbilityInstance->CanBeCanceled())
				{
					AsceAbilityInstance->CancelAbility(AbilitySpec.Handle, AbilityActorInfo.Get(), AsceAbilityInstance->GetCurrentActivationInfo(), bReplicateCancelAbility);
				}
				else
				{
					UE_LOG(LogAsceAbilitySystem, Error, TEXT("CancelAbilitiesByFunc: Can't cancel ability [%s] because CanBeCanceled is false."), *AsceAbilityInstance->GetName());
				}
			}
		}
	}
}

void UAsceAbilitySystemComponent::CancelInputActivatedAbilities(bool bReplicateCancelAbility)
{
	//取消所有通过输入来激活的能力, 本质上是调用的CancelAbilitiesByFunc
	auto ShouldCancelFunc = [this](const UAsceGameplayAbility* AsceAbility, FGameplayAbilitySpecHandle Handle)
	{
		const EAsceAbilityActivationPolicy ActivationPolicy = AsceAbility->GetActivationPolicy();
		return ((ActivationPolicy == EAsceAbilityActivationPolicy::OnInputTriggered) || (ActivationPolicy == EAsceAbilityActivationPolicy::WhileInputActive));
	};

	CancelAbilitiesByFunc(ShouldCancelFunc, bReplicateCancelAbility);
}

void UAsceAbilitySystemComponent::AbilitySpecInputPressed(FGameplayAbilitySpec& Spec)
{
	//当某个能力的被输入激活的时候调用
	
	Super::AbilitySpecInputPressed(Spec);

	// We don't support UGameplayAbility::bReplicateInputDirectly.
	// Use replicated events instead so that the WaitInputPress ability task works.
	//正常来说，此函数只会在本地调用，不会在服务器上调用，除非bReplicateInputDirectly开启，但是复制输入不太行，此处不启用
	//使用复制事件来代替，这样可以使等待输入的能力task正常工作
	
	if (Spec.IsActive())
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const UGameplayAbility* Instance = Spec.GetPrimaryInstance();
		FPredictionKey OriginalPredictionKey = Instance ? Instance->GetCurrentActivationInfo().GetActivationPredictionKey() : Spec.ActivationInfo.GetActivationPredictionKey();
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		// Invoke the InputPressed event. This is not replicated here. If someone is listening, they may replicate the InputPressed event to the server.
		// 调用输入按下事件，此处不会进行复制，如果别的地方在监听此事件，可能会将输入按下的事件复制到服务器
		InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Spec.Handle, OriginalPredictionKey);
	}
}

void UAsceAbilitySystemComponent::AbilitySpecInputReleased(FGameplayAbilitySpec& Spec)
{
	Super::AbilitySpecInputReleased(Spec);

	// We don't support UGameplayAbility::bReplicateInputDirectly.
	// Use replicated events instead so that the WaitInputRelease ability task works.
	if (Spec.IsActive())
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const UGameplayAbility* Instance = Spec.GetPrimaryInstance();
		FPredictionKey OriginalPredictionKey = Instance ? Instance->GetCurrentActivationInfo().GetActivationPredictionKey() : Spec.ActivationInfo.GetActivationPredictionKey();
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		// Invoke the InputReleased event. This is not replicated here. If someone is listening, they may replicate the InputReleased event to the server.
		InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Spec.Handle, OriginalPredictionKey);
	}
}

void UAsceAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	//某个按键被按下了，这个按键对应的Ability的GameplayTag被发送到此处
	if (InputTag.IsValid())
	{
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
			if (AbilitySpec.Ability && (AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag)))
			{
				InputPressedSpecHandles.AddUnique(AbilitySpec.Handle);
				InputHeldSpecHandles.AddUnique(AbilitySpec.Handle);
			}
		}
	}
}

void UAsceAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	//某个按键被释放了，对应的GameplayTag被发送到此处
	if (InputTag.IsValid())
	{
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
			if (AbilitySpec.Ability && (AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag)))
			{
				InputReleasedSpecHandles.AddUnique(AbilitySpec.Handle);
				InputHeldSpecHandles.Remove(AbilitySpec.Handle);
			}
		}
	}
}

void UAsceAbilitySystemComponent::ProcessAbilityInput(float DeltaTime, bool bGamePaused)
{
	//处理能力输入，每帧调用
	
	//检测此时是否阻塞输入
	if (HasMatchingGameplayTag(TAG_Gameplay_AbilityInputBlocked))
	{
		ClearAbilityInput();
		return;
	}

	static TArray<FGameplayAbilitySpecHandle> AbilitiesToActivate;
	AbilitiesToActivate.Reset();

	//@TODO: See if we can use FScopedServerAbilityRPCBatcher ScopedRPCBatcher in some of these loops

	//
	//InputHeldSpecHandles代表一直被按下输入的能力，只要该能力相关的按键被按下，该能力就会存在于这个集合中
	//InputPressedSpecHandles代表这一帧被按下输入的能力，这些能力如果输入不被释放，那么在下一帧会进入InputHeldSpecHandles中
	//InputPressedSpecHanldes和InputReleaseSpecHandles每帧都会清空
	
	// Process all abilities that activate when the input is held.
	// 处理所有的在输入持续时要激活的能力(按住一直激活，松开则取消激活)
	for (const FGameplayAbilitySpecHandle& SpecHandle : InputHeldSpecHandles)
	{
		if (const FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle))
		{
			if (AbilitySpec->Ability && !AbilitySpec->IsActive())
			{
				//这个能力是在持续按下一段时间之后才会激活的
				const UAsceGameplayAbility* AsceAbilityCDO = Cast<UAsceGameplayAbility>(AbilitySpec->Ability);
				if (AsceAbilityCDO && AsceAbilityCDO->GetActivationPolicy() == EAsceAbilityActivationPolicy::WhileInputActive)
				{
					AbilitiesToActivate.AddUnique(AbilitySpec->Handle);
				}
			}
		}
	}

	//
	// Process all abilities that had their input pressed this frame.
	// 处理这一帧按下输入时，激活的能力(按一下激活一下)
	for (const FGameplayAbilitySpecHandle& SpecHandle : InputPressedSpecHandles)
	{
		if (FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle))
		{
			if (AbilitySpec->Ability)
			{
				AbilitySpec->InputPressed = true;

				if (AbilitySpec->IsActive())
				{
					// Ability is active so pass along the input event.
					// 能力是已激活的，传递一下输入事件
					// 能力在刚按下的时候，已经处于激活状态了，这是一种及其特殊的情况，比如在持续按下某个能力时，按下另一个按键来二次触发此能力
					// 通常用于一些很特殊的技能
					AbilitySpecInputPressed(*AbilitySpec);
				}
				else
				{
					const UAsceGameplayAbility* AsceAbilityCDO = Cast<UAsceGameplayAbility>(AbilitySpec->Ability);

					if (AsceAbilityCDO && AsceAbilityCDO->GetActivationPolicy() == EAsceAbilityActivationPolicy::OnInputTriggered)
					{
						//只触发按下时触发的能力，如果按住不松手，随后便会在满足持续按下时间后进入持续按下的集合中，此时便会检测能力的激活状态来进行激活
						AbilitiesToActivate.AddUnique(AbilitySpec->Handle);
					}
				}
			}
		}
	}

	//
	// Try to activate all the abilities that are from presses and holds.
	// We do it all at once so that held inputs don't activate the ability
	// and then also send a input event to the ability because of the press.
	// 尝试激活所有按下触发激活以及满足持续按下时间激活的能力。 同时处理所有要激活的能力，由此持续按下并不会一直尝试反复激活能力，而是一直发送一个按下事件
	//
	
	for (const FGameplayAbilitySpecHandle& AbilitySpecHandle : AbilitiesToActivate)
	{
		TryActivateAbility(AbilitySpecHandle);
	}

	//
	// Process all abilities that had their input released this frame.
	// 处理这一帧释放按下输入的能力
	for (const FGameplayAbilitySpecHandle& SpecHandle : InputReleasedSpecHandles)
	{
		if (FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle))
		{
			if (AbilitySpec->Ability)
			{
				AbilitySpec->InputPressed = false;

				if (AbilitySpec->IsActive())
				{
					// Ability is active so pass along the input event.
					// 此能力在激活状态，传递一下事件
					AbilitySpecInputReleased(*AbilitySpec);
				}
			}
		}
	}

	//
	// Clear the cached ability handles.
	//
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
}

void UAsceAbilitySystemComponent::ClearAbilityInput()
{
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	InputHeldSpecHandles.Reset();
}

void UAsceAbilitySystemComponent::NotifyAbilityActivated(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability)
{
	//通知能力激活了
	Super::NotifyAbilityActivated(Handle, Ability);

	// if (UAsceGameplayAbility* AsceAbility = Cast<UAsceGameplayAbility>(Ability))
	// {
	// 	//将能力加入激活组
	// 	AddAbilityToActivationGroup(AsceAbility->GetActivationGroup(), AsceAbility);
	// }
}

void UAsceAbilitySystemComponent::NotifyAbilityFailed(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason)
{
	Super::NotifyAbilityFailed(Handle, Ability, FailureReason);

	if (APawn* Avatar = Cast<APawn>(GetAvatarActor()))
	{
		if (!Avatar->IsLocallyControlled() && Ability->IsSupportedForNetworking())
		{
			ClientNotifyAbilityFailed(Ability, FailureReason);
			return;
		}
	}

	HandleAbilityFailed(Ability, FailureReason);
}

void UAsceAbilitySystemComponent::NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, bool bWasCancelled)
{
	Super::NotifyAbilityEnded(Handle, Ability, bWasCancelled);

	// if (UAsceGameplayAbility* AsceAbility = Cast<UAsceGameplayAbility>(Ability))
	// {
	// 	RemoveAbilityFromActivationGroup(AsceAbility->GetActivationGroup(), AsceAbility);
	// }
}

void UAsceAbilitySystemComponent::ApplyAbilityBlockAndCancelTags(const FGameplayTagContainer& AbilityTags, UGameplayAbility* RequestingAbility, bool bEnableBlockTags, const FGameplayTagContainer& BlockTags, bool bExecuteCancelTags, const FGameplayTagContainer& CancelTags)
{
	//在preactivate中调用，在此处处理当前激活的能力要阻塞或者取消的能力
	FGameplayTagContainer ModifiedBlockTags = BlockTags;
	FGameplayTagContainer ModifiedCancelTags = CancelTags;

	if (TagRelationshipMapping)
	{
		// Use the mapping to expand the ability tags into block and cancel tag
		TagRelationshipMapping->GetAbilityTagsToBlockAndCancel(AbilityTags, &ModifiedBlockTags, &ModifiedCancelTags);
	}

	Super::ApplyAbilityBlockAndCancelTags(AbilityTags, RequestingAbility, bEnableBlockTags, ModifiedBlockTags, bExecuteCancelTags, ModifiedCancelTags);

	//@TODO: Apply any special logic like blocking input or movement
}

void UAsceAbilitySystemComponent::HandleChangeAbilityCanBeCanceled(const FGameplayTagContainer& AbilityTags, UGameplayAbility* RequestingAbility, bool bCanBeCanceled)
{
	Super::HandleChangeAbilityCanBeCanceled(AbilityTags, RequestingAbility, bCanBeCanceled);

	//@TODO: Apply any special logic like blocking input or movement
}

void UAsceAbilitySystemComponent::GetAdditionalActivationTagRequirements(const FGameplayTagContainer& AbilityTags, FGameplayTagContainer& OutActivationRequired, FGameplayTagContainer& OutActivationBlocked) const
{
	if (TagRelationshipMapping)
	{
		TagRelationshipMapping->GetRequiredAndBlockedActivationTags(AbilityTags, &OutActivationRequired, &OutActivationBlocked);
	}
}

void UAsceAbilitySystemComponent::SetTagRelationshipMapping(UAsceAbilityTagRelationshipMapping* NewMapping)
{
	TagRelationshipMapping = NewMapping;
}

void UAsceAbilitySystemComponent::ClientNotifyAbilityFailed_Implementation(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason)
{
	HandleAbilityFailed(Ability, FailureReason);
}

void UAsceAbilitySystemComponent::HandleAbilityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason)
{
	//UE_LOG(LogLyraAbilitySystem, Warning, TEXT("Ability %s failed to activate (tags: %s)"), *GetPathNameSafe(Ability), *FailureReason.ToString());

	//当某个能力激活失败的时候调用，传入失败的能力指针和失败原因
	
	if (const UAsceGameplayAbility* AsceAbility = Cast<const UAsceGameplayAbility>(Ability))
	{
		AsceAbility->OnAbilityFailedToActivate(FailureReason);
	}	
}

// bool UAsceAbilitySystemComponent::IsActivationGroupBlocked(EAsceAbilityActivationGroup Group) const
// {
// 	bool bBlocked = false;
//
// 	switch (Group)
// 	{
// 	case EAsceAbilityActivationGroup::Independent:
// 		// Independent abilities are never blocked.
// 		// 独立能力总是能被激活
// 		bBlocked = false;
// 		break;
//
// 	case EAsceAbilityActivationGroup::Exclusive_Replaceable:
// 	case EAsceAbilityActivationGroup::Exclusive_Blocking:
// 		// Exclusive abilities can activate if nothing is blocking.
// 		// 独特能力将能够激活如果没有另一个独特能力正在阻塞
// 		bBlocked = (ActivationGroupCounts[(uint8)EAsceAbilityActivationGroup::Exclusive_Blocking] > 0);
// 		break;
//
// 	default:
// 		checkf(false, TEXT("IsActivationGroupBlocked: Invalid ActivationGroup [%d]\n"), (uint8)Group);
// 		break;
// 	}
//
// 	return bBlocked;
// }

// void UAsceAbilitySystemComponent::AddAbilityToActivationGroup(EAsceAbilityActivationGroup Group, UAsceGameplayAbility* AsceAbility)
// {
// 	check(AsceAbility);
// 	check(ActivationGroupCounts[(uint8)Group] < INT32_MAX);
//
// 	ActivationGroupCounts[(uint8)Group]++;
//
// 	const bool bReplicateCancelAbility = false;
//
// 	switch (Group)
// 	{
// 	case EAsceAbilityActivationGroup::Independent:
// 		// Independent abilities do not cancel any other abilities.
// 		break;
//
// 	case EAsceAbilityActivationGroup::Exclusive_Replaceable:
// 	case EAsceAbilityActivationGroup::Exclusive_Blocking:
// 		CancelActivationGroupAbilities(EAsceAbilityActivationGroup::Exclusive_Replaceable, AsceAbility, bReplicateCancelAbility);
// 		break;
//
// 	default:
// 		checkf(false, TEXT("AddAbilityToActivationGroup: Invalid ActivationGroup [%d]\n"), (uint8)Group);
// 		break;
// 	}
//
// 	//逻辑上来说，独特能力将总是只有一个在激活状态
// 	const int32 ExclusiveCount = ActivationGroupCounts[(uint8)EAsceAbilityActivationGroup::Exclusive_Replaceable] + ActivationGroupCounts[(uint8)EAsceAbilityActivationGroup::Exclusive_Blocking];
// 	if (!ensure(ExclusiveCount <= 1))
// 	{
// 		UE_LOG(LogAsceAbilitySystem, Error, TEXT("AddAbilityToActivationGroup: Multiple exclusive abilities are running."));
// 	}
// }

// void UAsceAbilitySystemComponent::RemoveAbilityFromActivationGroup(EAsceAbilityActivationGroup Group, UAsceGameplayAbility* AsceAbility)
// {
// 	check(AsceAbility);
// 	check(ActivationGroupCounts[(uint8)Group] > 0);
//
// 	ActivationGroupCounts[(uint8)Group]--;
// }

// void UAsceAbilitySystemComponent::CancelActivationGroupAbilities(EAsceAbilityActivationGroup Group, UAsceGameplayAbility* IgnoreAsceAbility, bool bReplicateCancelAbility)
// {
// 	//依然是调用CancelAbilitiesByFunc，新激活的独特能力忽略，遍历其他所有的独特能力并取消激活
// 	auto ShouldCancelFunc = [this, Group, IgnoreAsceAbility](const UAsceGameplayAbility* AsceAbility, FGameplayAbilitySpecHandle Handle)
// 	{
// 		return ((AsceAbility->GetActivationGroup() == Group) && (AsceAbility != IgnoreAsceAbility));
// 	};
//
// 	CancelAbilitiesByFunc(ShouldCancelFunc, bReplicateCancelAbility);
// }

void UAsceAbilitySystemComponent::AddDynamicTagGameplayEffect(const FGameplayTag& Tag)
{
	
	//ASC上的tag通常是使用effect提供的tag的集合，DynamicTagGameplayEffect是专门用来添加动态tag的GE
	//实际上，并不是在一个GE上一直叠加tag，而是多个动态tag的GE不断叠加
	
	const TSubclassOf<UGameplayEffect> DynamicTagGE = UAsceAssetManager::GetSubclass(UAsceGameData::Get().DynamicTagGameplayEffect);
	if (!DynamicTagGE)
	{
		UE_LOG(LogAsceAbilitySystem, Warning, TEXT("AddDynamicTagGameplayEffect: Unable to find DynamicTagGameplayEffect [%s]."), *UAsceGameData::Get().DynamicTagGameplayEffect.GetAssetName());
		return;
	}

	const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingSpec(DynamicTagGE, 1.0f, MakeEffectContext());
	FGameplayEffectSpec* Spec = SpecHandle.Data.Get();

	if (!Spec)
	{
		UE_LOG(LogAsceAbilitySystem, Warning, TEXT("AddDynamicTagGameplayEffect: Unable to make outgoing spec for [%s]."), *GetNameSafe(DynamicTagGE));
		return;
	}

	Spec->DynamicGrantedTags.AddTag(Tag);

	ApplyGameplayEffectSpecToSelf(*Spec);
}

void UAsceAbilitySystemComponent::RemoveDynamicTagGameplayEffect(const FGameplayTag& Tag)
{
	const TSubclassOf<UGameplayEffect> DynamicTagGE = UAsceAssetManager::GetSubclass(UAsceGameData::Get().DynamicTagGameplayEffect);
	if (!DynamicTagGE)
	{
		UE_LOG(LogAsceAbilitySystem, Warning, TEXT("RemoveDynamicTagGameplayEffect: Unable to find gameplay effect [%s]."), *UAsceGameData::Get().DynamicTagGameplayEffect.GetAssetName());
		return;
	}

	FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag));
	Query.EffectDefinition = DynamicTagGE;

	RemoveActiveEffects(Query);
}

void UAsceAbilitySystemComponent::GetAbilityTargetData(const FGameplayAbilitySpecHandle AbilityHandle, FGameplayAbilityActivationInfo ActivationInfo, FGameplayAbilityTargetDataHandle& OutTargetDataHandle)
{
	//TargetData是由各种操作生产，比如鼠标点击选中，对某个范围内所有的actor进行选中，直接进行碰撞检测，最后生产出一个TargetData，里面存放了GA可以用于消费的目标数据（通常可以自定义的）
	//随后在能力的执行逻辑中，获取TargetData中的数据来决定能力的作用目标
	
	TSharedPtr<FAbilityReplicatedDataCache> ReplicatedData = AbilityTargetDataMap.Find(FGameplayAbilitySpecHandleAndPredictionKey(AbilityHandle, ActivationInfo.GetActivationPredictionKey()));
	if (ReplicatedData.IsValid())
	{
		OutTargetDataHandle = ReplicatedData->TargetData;
	}
}

