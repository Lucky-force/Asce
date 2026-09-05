// Fill out your copyright notice in the Description page of Project Settings.


#include "AscePawnExtensionComponent.h"

#include "AbilitySystem/AsceAbilitySystemComponent.h"
#include "Components/GameFrameworkComponentDelegates.h"
#include "Components/GameFrameworkComponentManager.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "AsceGameplayTags.h"
#include "AsceLogChannels.h"
#include "AscePawnData.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AscePawnExtensionComponent)

class FLifetimeProperty;
class UActorComponent;

const FName UAscePawnExtensionComponent::NAME_ActorFeatureName("PawnExtension");

UAscePawnExtensionComponent::UAscePawnExtensionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bCanEverTick = false;

	SetIsReplicatedByDefault(true);

	PawnData = nullptr;
	AbilitySystemComponent = nullptr;
}

void UAscePawnExtensionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAscePawnExtensionComponent, PawnData);
}

void UAscePawnExtensionComponent::OnRegister()
{
	Super::OnRegister();
	
	//注册的时候检查是不是挂在Pawn身上并且只有一个该组件
	const APawn* Pawn = GetPawn<APawn>();
	ensureAlwaysMsgf((Pawn != nullptr), TEXT("AscePawnExtensionComponent on [%s] can only be added to Pawn actors."), *GetNameSafe(GetOwner()));

	TArray<UActorComponent*> PawnExtensionComponents;
	Pawn->GetComponents(UAscePawnExtensionComponent::StaticClass(), PawnExtensionComponents);
	ensureAlwaysMsgf((PawnExtensionComponents.Num() == 1), TEXT("Only one LyraPawnExtensionComponent should exist on [%s]."), *GetNameSafe(GetOwner()));

	// Register with the init state system early, this will only work if this is a game world
	// 注册初始化状态系统
	RegisterInitStateFeature();
}

void UAscePawnExtensionComponent::BeginPlay()
{
	Super::BeginPlay();

	// Listen for changes to all features
	BindOnActorInitStateChanged(NAME_None, FGameplayTag(), false);
	
	// Notifies state manager that we have spawned, then try rest of default initialization
	// 尝试转移到已生产状态
	ensure(TryToChangeInitState(AsceGameplayTags::InitState_Spawned));
	CheckDefaultInitialization();
}

void UAscePawnExtensionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UninitializeAbilitySystem();
	UnregisterInitStateFeature();

	Super::EndPlay(EndPlayReason);
}

void UAscePawnExtensionComponent::SetPawnData(const UAscePawnData* InPawnData)
{
	//只会在服务器上调用
	check(InPawnData);

	APawn* Pawn = GetPawnChecked<APawn>();

	if (Pawn->GetLocalRole() != ROLE_Authority)
	{
		return;
	}

	if (PawnData)
	{
		UE_LOG(LogAsce, Error, TEXT("Trying to set PawnData [%s] on pawn [%s] that already has valid PawnData [%s]."), *GetNameSafe(InPawnData), *GetNameSafe(Pawn), *GetNameSafe(PawnData));
		return;
	}

	PawnData = InPawnData;

	Pawn->ForceNetUpdate();

	CheckDefaultInitialization();
}

void UAscePawnExtensionComponent::OnRep_PawnData()
{
	CheckDefaultInitialization();
}

void UAscePawnExtensionComponent::InitializeAbilitySystem(UAsceAbilitySystemComponent* InASC, AActor* InOwnerActor)
{
	check(InASC);
	check(InOwnerActor);

	if (AbilitySystemComponent == InASC)
	{
		// The ability system component hasn't changed.
		return;
	}

	if (AbilitySystemComponent)
	{
		// Clean up the old ability system component.
		UninitializeAbilitySystem();
	}

	APawn* Pawn = GetPawnChecked<APawn>();
	AActor* ExistingAvatar = InASC->GetAvatarActor();

	UE_LOG(LogAsce, Verbose, TEXT("Setting up ASC [%s] on pawn [%s] owner [%s], existing [%s] "), *GetNameSafe(InASC), *GetNameSafe(Pawn), *GetNameSafe(InOwnerActor), *GetNameSafe(ExistingAvatar));

	if ((ExistingAvatar != nullptr) && (ExistingAvatar != Pawn))
	{
		UE_LOG(LogAsce, Log, TEXT("Existing avatar (authority=%d)"), ExistingAvatar->HasAuthority() ? 1 : 0);

		// There is already a pawn acting as the ASC's avatar, so we need to kick it out
		// This can happen on clients if they're lagged: their new pawn is spawned + possessed before the dead one is removed
		// 上一个Avatar还在ASC上，需要先清除上一个Avatar，在客户端网络延迟较高时可能发生，新的Pawn已经生产并且被Controller占用
		// 但是上一个死去的Pawn还没有移除
		ensure(!ExistingAvatar->HasAuthority());//保证不是在服务器上发生这种情况

		if (UAscePawnExtensionComponent* OtherExtensionComponent = FindPawnExtensionComponent(ExistingAvatar))
		{
			OtherExtensionComponent->UninitializeAbilitySystem();
		}
	}

	AbilitySystemComponent = InASC;
	AbilitySystemComponent->InitAbilityActorInfo(InOwnerActor, Pawn);//初始化ActorInfo

	if (ensure(PawnData))
	{
		InASC->SetTagRelationshipMapping(PawnData->TagRelationshipMapping);
	}

	OnAbilitySystemInitialized.Broadcast();
}

void UAscePawnExtensionComponent::UninitializeAbilitySystem()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	// Uninitialize the ASC if we're still the avatar actor (otherwise another pawn already did it when they became the avatar actor)
	// 仅在当前拥有者Pawn依然是ASC的Avatar时清除
	if (AbilitySystemComponent->GetAvatarActor() == GetOwner())
	{
		FGameplayTagContainer AbilityTypesToIgnore;
		AbilityTypesToIgnore.AddTag(AsceGameplayTags::Ability_Behavior_SurvivesDeath);

		AbilitySystemComponent->CancelAbilities(nullptr, &AbilityTypesToIgnore);
		AbilitySystemComponent->ClearAbilityInput();
		AbilitySystemComponent->RemoveAllGameplayCues();

		if (AbilitySystemComponent->GetOwnerActor() != nullptr)
		{
			AbilitySystemComponent->SetAvatarActor(nullptr);
		}
		else
		{
			// If the ASC doesn't have a valid owner, we need to clear *all* actor info, not just the avatar pairing
			AbilitySystemComponent->ClearActorInfo();
		}

		OnAbilitySystemUninitialized.Broadcast();
	}

	AbilitySystemComponent = nullptr;
}

void UAscePawnExtensionComponent::HandleControllerChanged()
{
	//在Controller更新时刷新一下ASC的ActorInfo，刷新函数会认为Avatar是正确的，然后根据Avatar来刷新其他数据
	if (AbilitySystemComponent && (AbilitySystemComponent->GetAvatarActor() == GetPawnChecked<APawn>()))
	{
		ensure(AbilitySystemComponent->AbilityActorInfo->OwnerActor == AbilitySystemComponent->GetOwnerActor());
		if (AbilitySystemComponent->GetOwnerActor() == nullptr)
		{
			UninitializeAbilitySystem();
		}
		else
		{
			AbilitySystemComponent->RefreshAbilityActorInfo();
		}
	}

	CheckDefaultInitialization();
}

void UAscePawnExtensionComponent::HandlePlayerStateReplicated()
{
	CheckDefaultInitialization();
}

void UAscePawnExtensionComponent::SetupPlayerInputComponent()
{
	CheckDefaultInitialization();
}

void UAscePawnExtensionComponent::CheckDefaultInitialization()
{
	//此函数会尝试转移状态，在转移状态之前，会先通知一下所有Feature的Implementer也转移一下状态到达最新的状态
	
	
	// Before checking our progress, try progressing any other features we might depend on
	// 先广播所有Feature的Implementer，让它们检查一下
	CheckDefaultInitializationForImplementers();

	static const TArray<FGameplayTag> StateChain = { AsceGameplayTags::InitState_Spawned, AsceGameplayTags::InitState_DataAvailable, AsceGameplayTags::InitState_DataInitialized, AsceGameplayTags::InitState_GameplayReady };

	// This will try to progress from spawned (which is only set in BeginPlay) through the data initialization stages until it gets to gameplay ready
	// 尝试状态转移
	ContinueInitStateChain(StateChain);
}

bool UAscePawnExtensionComponent::CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const
{
	check(Manager);

	APawn* Pawn = GetPawn<APawn>();
	if (!CurrentState.IsValid() && DesiredState == AsceGameplayTags::InitState_Spawned)
	{
		// As long as we are on a valid pawn, we count as spawned
		//检查是否能转移到Spawned状态，只要Pawn有效就通过
		if (Pawn)
		{
			return true;
		}
	}
	if (CurrentState == AsceGameplayTags::InitState_Spawned && DesiredState == AsceGameplayTags::InitState_DataAvailable)
	{	
		//希望从Spawned状态转移到DataAvailable状态
		// Pawn data is required.
		if (!PawnData)
		{
			return false;
		}

		const bool bHasAuthority = Pawn->HasAuthority();
		const bool bIsLocallyControlled = Pawn->IsLocallyControlled();

		// 单机模式下的Pawn或者是主控端的Pawn，必须保证Controller有效
		if (bHasAuthority || bIsLocallyControlled)
		{
			// Check for being possessed by a controller.
			if (!GetController<AController>())
			{
				return false;
			}
		}

		return true;
	}
	else if (CurrentState == AsceGameplayTags::InitState_DataAvailable && DesiredState == AsceGameplayTags::InitState_DataInitialized)
	{
		// Transition to initialize if all features have their data available
		// 检查所有其他Feature的Implementer到达DataAvilable状态
		return Manager->HaveAllFeaturesReachedInitState(Pawn, AsceGameplayTags::InitState_DataAvailable);
	}
	else if (CurrentState == AsceGameplayTags::InitState_DataInitialized && DesiredState == AsceGameplayTags::InitState_GameplayReady)
	{
		//数据初始化完毕就直接运行允许转移到GameplayReady
		return true;
	}

	return false;
}

void UAscePawnExtensionComponent::HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState)
{
	if (DesiredState == AsceGameplayTags::InitState_DataInitialized)
	{
		// This is currently all handled by other components listening to this state change
	}
}

void UAscePawnExtensionComponent::OnActorInitStateChanged(const FActorInitStateChangedParams& Params)
{
	// If another feature is now in DataAvailable, see if we should transition to DataInitialized
	if (Params.FeatureName != NAME_ActorFeatureName)
	{
		if (Params.FeatureState == AsceGameplayTags::InitState_DataAvailable)
		{
			CheckDefaultInitialization();
		}
	}
}

void UAscePawnExtensionComponent::OnAbilitySystemInitialized_RegisterAndCall(FSimpleMulticastDelegate::FDelegate Delegate)
{
	if (!OnAbilitySystemInitialized.IsBoundToObject(Delegate.GetUObject()))
	{
		OnAbilitySystemInitialized.Add(Delegate);
	}

	if (AbilitySystemComponent)
	{
		Delegate.Execute();
	}
}

void UAscePawnExtensionComponent::OnAbilitySystemUninitialized_Register(FSimpleMulticastDelegate::FDelegate Delegate)
{
	if (!OnAbilitySystemUninitialized.IsBoundToObject(Delegate.GetUObject()))
	{
		OnAbilitySystemUninitialized.Add(Delegate);
	}
}

