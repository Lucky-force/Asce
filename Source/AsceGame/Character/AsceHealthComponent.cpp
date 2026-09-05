#include "Character/AsceHealthComponent.h"

#include "AbilitySystem/Attributes/AsceAttributeSet.h"
#include "AsceLogChannels.h"
#include "System/AsceAssetManager.h"
#include "System/AsceGameData.h"
#include "AsceGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "AbilitySystem/AsceAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/AsceHealthSet.h"
#include "Messages/AsceVerbMessage.h"
#include "Messages/AsceVerbMessageHelpers.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceHealthComponent)

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Asce_Elimination_Message, "Asce.Elimination.Message");


UAsceHealthComponent::UAsceHealthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bCanEverTick = false;

	SetIsReplicatedByDefault(true);

	AbilitySystemComponent = nullptr;
	HealthSet = nullptr;
	DeathState = EAsceDeathState::NotDead;
}

void UAsceHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAsceHealthComponent, DeathState);
}

void UAsceHealthComponent::OnUnregister()
{
	UninitializeFromAbilitySystem();

	Super::OnUnregister();
}

void UAsceHealthComponent::InitializeWithAbilitySystem(UAsceAbilitySystemComponent* InASC)
{
	AActor* Owner = GetOwner();
	check(Owner);

	if (AbilitySystemComponent)
	{
		UE_LOG(LogAsce, Error, TEXT("AsceHealthComponent: Health component for owner [%s] has already been initialized with an ability system."), *GetNameSafe(Owner));
		return;
	}

	AbilitySystemComponent = InASC;
	if (!AbilitySystemComponent)
	{
		UE_LOG(LogAsce, Error, TEXT("AsceHealthComponent: Cannot initialize health component for owner [%s] with NULL ability system."), *GetNameSafe(Owner));
		return;
	}

	HealthSet = AbilitySystemComponent->GetSet<UAsceHealthSet>();
	if (!HealthSet)
	{
		UE_LOG(LogAsce, Error, TEXT("AsceHealthComponent: Cannot initialize health component for owner [%s] with NULL health set on the ability system."), *GetNameSafe(Owner));
		return;
	}

	
	//给ASC上的HealthAttributeSet绑委托
	// Register to listen for attribute changes.
	HealthSet->OnHealthChanged.AddUObject(this, &ThisClass::HandleHealthChanged);
	HealthSet->OnMaxHealthChanged.AddUObject(this, &ThisClass::HandleMaxHealthChanged);
	HealthSet->OnOutOfHealth.AddUObject(this, &ThisClass::HandleOutOfHealth);

	// TEMP: Reset attributes to default values.  Eventually this will be driven by a spread sheet.
	AbilitySystemComponent->SetNumericAttributeBase(UAsceHealthSet::GetHealthAttribute(), HealthSet->GetMaxHealth());

	ClearGameplayTags();

	OnHealthChanged.Broadcast(this, HealthSet->GetHealth(), HealthSet->GetHealth(), nullptr);
	OnMaxHealthChanged.Broadcast(this, HealthSet->GetHealth(), HealthSet->GetHealth(), nullptr);
}

void UAsceHealthComponent::UninitializeFromAbilitySystem()
{
	ClearGameplayTags();
	
	//解绑委托
	if (HealthSet)
	{
		HealthSet->OnHealthChanged.RemoveAll(this);
		HealthSet->OnMaxHealthChanged.RemoveAll(this);
		HealthSet->OnOutOfHealth.RemoveAll(this);
	}

	//清空指针
	HealthSet = nullptr;
	AbilitySystemComponent = nullptr;
}

void UAsceHealthComponent::ClearGameplayTags()
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->SetLooseGameplayTagCount(AsceGameplayTags::Status_Death_Dying, 0);
		AbilitySystemComponent->SetLooseGameplayTagCount(AsceGameplayTags::Status_Death_Dead, 0);
	}
}

float UAsceHealthComponent::GetHealth() const
{
	return (HealthSet ? HealthSet->GetHealth() : 0.0f);
}

float UAsceHealthComponent::GetMaxHealth() const
{
	return (HealthSet ? HealthSet->GetMaxHealth() : 0.0f);
}

float UAsceHealthComponent::GetHealthNormalized() const
{
	if (HealthSet)
	{
		const float Health = HealthSet->GetHealth();
		const float MaxHealth = HealthSet->GetMaxHealth();

		return ((MaxHealth > 0.0f) ? (Health / MaxHealth) : 0.0f);
	}

	return 0.0f;
}

void UAsceHealthComponent::HandleHealthChanged(AActor* DamageInstigator, AActor* DamageCauser, const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue)
{
	//这里只广播了旧的值，新的值，以及触发者Instigator
	OnHealthChanged.Broadcast(this, OldValue, NewValue, DamageInstigator);
}

void UAsceHealthComponent::HandleMaxHealthChanged(AActor* DamageInstigator, AActor* DamageCauser, const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue)
{
	OnMaxHealthChanged.Broadcast(this, OldValue, NewValue, DamageInstigator);
}

void UAsceHealthComponent::HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser, const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue)
{
#if WITH_SERVER_CODE
	//只能在服务器上调用
	if (AbilitySystemComponent && DamageEffectSpec)
	{
		// Send the "GameplayEvent.Death" gameplay event through the owner's ability system.  This can be used to trigger a death gameplay ability.
		// 发送死亡事件到ASC用来触发死亡能力
		{
			//从EffectSpec中取出InstigatorTags和TargetTags以及EffectContextHandle
			FGameplayEventData Payload;
			Payload.EventTag = AsceGameplayTags::GameplayEvent_Death;
			Payload.Instigator = DamageInstigator;
			Payload.Target = AbilitySystemComponent->GetAvatarActor();
			Payload.OptionalObject = DamageEffectSpec->Def;
			Payload.ContextHandle = DamageEffectSpec->GetEffectContext();
			Payload.InstigatorTags = *DamageEffectSpec->CapturedSourceTags.GetAggregatedTags();
			Payload.TargetTags = *DamageEffectSpec->CapturedTargetTags.GetAggregatedTags();
			Payload.EventMagnitude = DamageMagnitude;

			FScopedPredictionWindow NewScopedWindow(AbilitySystemComponent, true);
			AbilitySystemComponent->HandleGameplayEvent(Payload.EventTag, &Payload);
		}

		// Send a standardized verb message that other systems can observe
		// ASC用的GameplayEvent，其他系统用的是更轻量化的verb message，这里也发一下这个信息
		{
			FAsceVerbMessage Message;
			Message.Verb = TAG_Asce_Elimination_Message;
			Message.Instigator = DamageInstigator;
			Message.InstigatorTags = *DamageEffectSpec->CapturedSourceTags.GetAggregatedTags();
			Message.Target = UAsceVerbMessageHelpers::GetPlayerStateFromObject(AbilitySystemComponent->GetAvatarActor());
			Message.TargetTags = *DamageEffectSpec->CapturedTargetTags.GetAggregatedTags();
			//@TODO: Fill out context tags, and any non-ability-system source/instigator tags
			//@TODO: Determine if it's an opposing team kill, self-own, team kill, etc...

			UGameplayMessageSubsystem& MessageSystem = UGameplayMessageSubsystem::Get(GetWorld());
			MessageSystem.BroadcastMessage(Message.Verb, Message);
		}

		//@TODO: assist messages (could compute from damage dealt elsewhere)?
	}

#endif // #if WITH_SERVER_CODE
}

void UAsceHealthComponent::OnRep_DeathState(EAsceDeathState OldDeathState)
{
	//带参数的OnRep，服务器把DeathState复制到客户端后会调用OnRep函数，这里声明带旧值的参数，因此还会传入旧值
	//我们不希望直接通过复制来修改死亡状态，因为死亡状态涉及到状态转移，这个过程需要调用一些函数，直接复制状态值不太好
	//因此这里记录旧的状态，新的状态，通过两个状态的差异来模拟状态转移
	const EAsceDeathState NewDeathState = DeathState;

	// Revert the death state for now since we rely on StartDeath and FinishDeath to change it.
	// 回退到上一个死亡状态
	DeathState = OldDeathState;

	if (OldDeathState > NewDeathState)
	{
		//旧状态在新状态后面，不可能，除非服务器尝试复活一个死人
		// The server is trying to set us back but we've already predicted past the server state.
		UE_LOG(LogAsce, Warning, TEXT("AsceHealthComponent: Predicted past server death state [%d] -> [%d] for owner [%s]."), (uint8)OldDeathState, (uint8)NewDeathState, *GetNameSafe(GetOwner()));
		return;
	}

	if (OldDeathState == EAsceDeathState::NotDead)
	{
		if (NewDeathState == EAsceDeathState::DeathStarted)
		{
			//刚开始死亡
			StartDeath();
		}
		else if (NewDeathState == EAsceDeathState::DeathFinished)
		{
			//已死亡，直接调用两个状态转移的函数
			StartDeath();
			FinishDeath();
		}
		else
		{
			UE_LOG(LogAsce, Error, TEXT("AsceHealthComponent: Invalid death transition [%d] -> [%d] for owner [%s]."), (uint8)OldDeathState, (uint8)NewDeathState, *GetNameSafe(GetOwner()));
		}
	}
	else if (OldDeathState == EAsceDeathState::DeathStarted)
	{
		if (NewDeathState == EAsceDeathState::DeathFinished)
		{
			FinishDeath();
		}
		else
		{
			UE_LOG(LogAsce, Error, TEXT("AsceHealthComponent: Invalid death transition [%d] -> [%d] for owner [%s]."), (uint8)OldDeathState, (uint8)NewDeathState, *GetNameSafe(GetOwner()));
		}
	}

	ensureMsgf((DeathState == NewDeathState), TEXT("AsceHealthComponent: Death transition failed [%d] -> [%d] for owner [%s]."), (uint8)OldDeathState, (uint8)NewDeathState, *GetNameSafe(GetOwner()));
}

void UAsceHealthComponent::StartDeath()
{
	if (DeathState != EAsceDeathState::NotDead)
	{
		return;
	}

	DeathState = EAsceDeathState::DeathStarted;

	if (AbilitySystemComponent)
	{
		// 给Avatar加一个死亡中的标签
		AbilitySystemComponent->SetLooseGameplayTagCount(AsceGameplayTags::Status_Death_Dying, 1);
	}

	AActor* Owner = GetOwner();
	check(Owner);

	OnDeathStarted.Broadcast(Owner);

	//服务器的话就网络更新一下
	Owner->ForceNetUpdate();
}

void UAsceHealthComponent::FinishDeath()
{
	if (DeathState != EAsceDeathState::DeathStarted)
	{
		return;
	}

	DeathState = EAsceDeathState::DeathFinished;

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->SetLooseGameplayTagCount(AsceGameplayTags::Status_Death_Dead, 1);
	}

	AActor* Owner = GetOwner();
	check(Owner);

	OnDeathFinished.Broadcast(Owner);

	Owner->ForceNetUpdate();
}

void UAsceHealthComponent::DamageSelfDestruct(bool bFellOutOfWorld)
{
	if ((DeathState == EAsceDeathState::NotDead) && AbilitySystemComponent)
	{
		const TSubclassOf<UGameplayEffect> DamageGE = UAsceAssetManager::GetSubclass(UAsceGameData::Get().DamageGameplayEffect_SetByCaller);
		if (!DamageGE)
		{
			UE_LOG(LogAsce, Error, TEXT("AsceHealthComponent: DamageSelfDestruct failed for owner [%s]. Unable to find gameplay effect [%s]."), *GetNameSafe(GetOwner()), *UAsceGameData::Get().DamageGameplayEffect_SetByCaller.GetAssetName());
			return;
		}

		FGameplayEffectSpecHandle SpecHandle = AbilitySystemComponent->MakeOutgoingSpec(DamageGE, 1.0f, AbilitySystemComponent->MakeEffectContext());
		FGameplayEffectSpec* Spec = SpecHandle.Data.Get();

		if (!Spec)
		{
			UE_LOG(LogAsce, Error, TEXT("AsceHealthComponent: DamageSelfDestruct failed for owner [%s]. Unable to make outgoing spec for [%s]."), *GetNameSafe(GetOwner()), *GetNameSafe(DamageGE));
			return;
		}

		Spec->AddDynamicAssetTag(TAG_Gameplay_DamageSelfDestruct);

		if (bFellOutOfWorld)
		{
			Spec->AddDynamicAssetTag(TAG_Gameplay_FellOutOfWorld);
		}

		const float DamageAmount = GetMaxHealth();

		Spec->SetSetByCallerMagnitude(AsceGameplayTags::SetByCaller_Damage, DamageAmount);
		AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec);
	}
}

