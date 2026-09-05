// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceGameplayAbility.h"
#include "AsceLogChannels.h"
#include "AbilitySystem/AsceAbilitySystemComponent.h"
#include "AbilitySystemLog.h"
#include "Player/AscePlayerController.h"
#include "Character/AsceCharacter.h"
#include "AsceGameplayTags.h"
#include "AsceAbilityCost.h"
#include "Character/AsceHeroComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemGlobals.h"
#include "AsceAbilitySimpleFailureMessage.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "AbilitySystem/AsceAbilitySourceInterface.h"
#include "AbilitySystem/AsceGameplayEffectContext.h"
#include "Physics/PhysicalMaterialWithTags.h"
#include "GameFramework/PlayerState.h"
#include "Camera/AsceCameraMode.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceGameplayAbility)

#define ENSURE_ABILITY_IS_INSTANTIATED_OR_RETURN(FunctionName, ReturnValue)																				\
{																																						\
	if (!ensure(IsInstantiated()))																														\
	{																																					\
		ABILITY_LOG(Error, TEXT("%s: " #FunctionName " cannot be called on a non-instanced ability. Check the instancing policy."), *GetPathName());	\
		return ReturnValue;																																\
	}																																					\
}

UE_DEFINE_GAMEPLAY_TAG(TAG_ABILITY_SIMPLE_FAILURE_MESSAGE, "Ability.UserFacingSimpleActivateFail.Message");
UE_DEFINE_GAMEPLAY_TAG(TAG_ABILITY_PLAY_MONTAGE_FAILURE_MESSAGE, "Ability.PlayMontageOnActivateFail.Message");

UAsceGameplayAbility::UAsceGameplayAbility(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ClientOrServer;

	ActivationPolicy = EAsceAbilityActivationPolicy::OnInputTriggered;
	//ActivationGroup = EAsceAbilityActivationGroup::Independent;

	bLogCancelation = false;

	ActiveCameraMode = nullptr;
}

UAsceAbilitySystemComponent* UAsceGameplayAbility::GetAsceAbilitySystemComponentFromActorInfo() const
{
	return (CurrentActorInfo ? Cast<UAsceAbilitySystemComponent>(CurrentActorInfo->AbilitySystemComponent.Get()) : nullptr);
}

AAscePlayerController* UAsceGameplayAbility::GetAscePlayerControllerFromActorInfo() const
{
	return (CurrentActorInfo ? Cast<AAscePlayerController>(CurrentActorInfo->PlayerController.Get()) : nullptr);
}

AController* UAsceGameplayAbility::GetControllerFromActorInfo() const
{
	if (CurrentActorInfo)
	{
		if (AController* PC = CurrentActorInfo->PlayerController.Get())
		{
			return PC;
		}

		// Look for a player controller or pawn in the owner chain.
		AActor* TestActor = CurrentActorInfo->OwnerActor.Get();
		while (TestActor)
		{
			if (AController* C = Cast<AController>(TestActor))
			{
				return C;
			}

			if (APawn* Pawn = Cast<APawn>(TestActor))
			{
				return Pawn->GetController();
			}

			TestActor = TestActor->GetOwner();
		}
	}

	return nullptr;
}

AAsceCharacter* UAsceGameplayAbility::GetAsceCharacterFromActorInfo() const
{
	return (CurrentActorInfo ? Cast<AAsceCharacter>(CurrentActorInfo->AvatarActor.Get()) : nullptr);
}

UAsceHeroComponent* UAsceGameplayAbility::GetHeroComponentFromActorInfo() const
{
	return (CurrentActorInfo ? UAsceHeroComponent::FindHeroComponent(CurrentActorInfo->AvatarActor.Get()) : nullptr);
}

void UAsceGameplayAbility::NativeOnAbilityFailedToActivate(const FGameplayTagContainer& FailedReason) const
{
	//此能力激活失败时的行为
	bool bSimpleFailureFound = false;
	for (FGameplayTag Reason : FailedReason)
	{
		if (!bSimpleFailureFound)
		{
			if (const FText* pUserFacingMessage = FailureTagToUserFacingMessages.Find(Reason))
			{
				FAsceAbilitySimpleFailureMessage Message;
				Message.PlayerController = GetActorInfo().PlayerController.Get();
				Message.FailureTags = FailedReason;
				Message.UserFacingReason = *pUserFacingMessage;

				UGameplayMessageSubsystem& MessageSystem = UGameplayMessageSubsystem::Get(GetWorld());
				MessageSystem.BroadcastMessage(TAG_ABILITY_SIMPLE_FAILURE_MESSAGE, Message);
				bSimpleFailureFound = true;
			}
		}
		
		if (UAnimMontage* pMontage = FailureTagToAnimMontage.FindRef(Reason))
		{
			FAsceAbilityMontageFailureMessage Message;
			Message.PlayerController = GetActorInfo().PlayerController.Get();
			Message.AvatarActor = GetActorInfo().AvatarActor.Get();
			Message.FailureTags = FailedReason;
			Message.FailureMontage = pMontage;

			UGameplayMessageSubsystem& MessageSystem = UGameplayMessageSubsystem::Get(GetWorld());
			MessageSystem.BroadcastMessage(TAG_ABILITY_PLAY_MONTAGE_FAILURE_MESSAGE, Message);
		}
	}
}

bool UAsceGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
	{
		return false;
	}

	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	//@TODO Possibly remove after setting up tag relationships
	//已移除激活组的判断，现在GA的PreActivate中调用的ApplyAbilityBlockAndCancelTags函数进行了重写，会根据当前ASC上的RelationShipMapping来处理能力的阻塞和取消
	// UAsceAbilitySystemComponent* AsceASC = CastChecked<UAsceAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get());
	// if (AsceASC->IsActivationGroupBlocked(ActivationGroup))
	// {
	// 	if (OptionalRelevantTags)
	// 	{
	// 		OptionalRelevantTags->AddTag(AsceGameplayTags::Ability_ActivateFail_ActivationGroup);
	// 	}
	// 	return false;
	// }

	return true;
}

void UAsceGameplayAbility::SetCanBeCanceled(bool bCanBeCanceled)
{
	//本来这里是对于激活组类型为可被取代的GA就不能设置为无法取消的，但是现在使用relationshipmapping，GA本身不知道自身是否可以被取消
	//这个问题看看之后怎么处理吧

	Super::SetCanBeCanceled(bCanBeCanceled);
}

void UAsceGameplayAbility::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);

	K2_OnAbilityAdded();

	TryActivateAbilityOnSpawn(ActorInfo, Spec);
}

void UAsceGameplayAbility::OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	K2_OnAbilityRemoved();

	Super::OnRemoveAbility(ActorInfo, Spec);
}

void UAsceGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UAsceGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	ClearCameraMode();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UAsceGameplayAbility::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
	//先检查原本的代价
	if (!Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags) || !ActorInfo)
	{
		return false;
	}

	// Verify we can afford any additional costs
	// 再检查是否能支付额外的代价
	for (const TObjectPtr<UAsceAbilityCost>& AdditionalCost : AdditionalCosts)
	{
		if (AdditionalCost != nullptr)
		{
			if (!AdditionalCost->CheckCost(this, Handle, ActorInfo, /*inout*/ OptionalRelevantTags))
			{
				return false;
			}
		}
	}

	return true;
}

void UAsceGameplayAbility::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	Super::ApplyCost(Handle, ActorInfo, ActivationInfo);

	check(ActorInfo);

	// Used to determine if the ability actually hit a target (as some costs are only spent on successful attempts)
	// 此lambda用于检测此时是否实际上击中了一个目标（部分能力在命中目标时才会支付代价）
	auto DetermineIfAbilityHitTarget = [&]()
	{
		//必须由网络权威的服务器来检查命中情况
		if (ActorInfo->IsNetAuthority())
		{
			if (UAsceAbilitySystemComponent* ASC = Cast<UAsceAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()))
			{
				FGameplayAbilityTargetDataHandle TargetData;
				ASC->GetAbilityTargetData(Handle, ActivationInfo, TargetData);
				for (int32 TargetDataIdx = 0; TargetDataIdx < TargetData.Data.Num(); ++TargetDataIdx)
				{
					if (UAbilitySystemBlueprintLibrary::TargetDataHasHitResult(TargetData, TargetDataIdx))
					{
						return true;
					}
				}
			}
		}

		return false;
	};

	// Pay any additional costs
	bool bAbilityHitTarget = false;
	bool bHasDeterminedIfAbilityHitTarget = false;
	for (const TObjectPtr<UAsceAbilityCost>& AdditionalCost : AdditionalCosts)
	{
		if (AdditionalCost != nullptr)
		{
			if (AdditionalCost->ShouldOnlyApplyCostOnHit())
			{
				if (!bHasDeterminedIfAbilityHitTarget)
				{
					bAbilityHitTarget = DetermineIfAbilityHitTarget();
					bHasDeterminedIfAbilityHitTarget = true;
				}

				if (!bAbilityHitTarget)
				{
					continue;
				}
			}

			AdditionalCost->ApplyCost(this, Handle, ActorInfo, ActivationInfo);
		}
	}
}

FGameplayEffectContextHandle UAsceGameplayAbility::MakeEffectContext(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const
{
	//此函数获取此能力造成影响的上下文信息
	
	FGameplayEffectContextHandle ContextHandle = Super::MakeEffectContext(Handle, ActorInfo);

	//AbilitySpecHandle仅仅是一个数字，标识了一个ASC上的某个GASpec，此处传入ActorInfo，是默认了从这个ActorInfo所对应的ASC上使用此Handle来查询GASpec
	//GASpec相当于技能槽位，即技能还没有释放出去时的状态，记录了能力的各种相对于当前ASC的实例化数据。当释放技能的时候，则会创建一个GA并使用这些实例化数据来初始化（对于PerInstanced类型）
	
	
	//EffectContext记录了对于这个能力会造成的影响的信息
	
	FAsceGameplayEffectContext* EffectContext = FAsceGameplayEffectContext::ExtractEffectContext(ContextHandle);
	check(EffectContext);

	check(ActorInfo);

	AActor* EffectCauser = nullptr;
	const IAsceAbilitySourceInterface* AbilitySource = nullptr;
	float SourceLevel = 0.0f;
	
	//ActorInfo存放了角色信息，比如avatar，动画实例，骨骼网格体，controller等
	//Handle指向
	
	GetAbilitySource(Handle, ActorInfo, /*out*/ SourceLevel, /*out*/ AbilitySource, /*out*/ EffectCauser);

	UObject* SourceObject = GetSourceObject(Handle, ActorInfo);

	AActor* Instigator = ActorInfo ? ActorInfo->OwnerActor.Get() : nullptr;

	//Causer是释放能力的物理对象, 比如子弹
	//Source是能力来源，比如玩家使用的武器为玩家提供了这种造成伤害的能力，那么这个武器就是能力的来源
	//Instigator是释放能力的角色
	//Causer这里用的Avatar，Instigator用的PlayerState
	
	//Source和AbilitySource实际上都是能力的提供者（来源），这里Source是广义的UObject，而AbilitySource是实现了对应接口的UObject
	//将AbilitySource也存下来，可以通过AbilitySource来获取自定义的更多信息
	
	EffectContext->SetAbilitySource(AbilitySource, SourceLevel);
	EffectContext->AddInstigator(Instigator, EffectCauser);
	EffectContext->AddSourceObject(SourceObject);

	return ContextHandle;
}

void UAsceGameplayAbility::ApplyAbilityTagsToGameplayEffectSpec(FGameplayEffectSpec& Spec, FGameplayAbilitySpec* AbilitySpec) const
{
	Super::ApplyAbilityTagsToGameplayEffectSpec(Spec, AbilitySpec);
	
	
	if (const FHitResult* HitResult = Spec.GetContext().GetHitResult())
	{
		if (const UPhysicalMaterialWithTags* PhysMatWithTags = Cast<const UPhysicalMaterialWithTags>(HitResult->PhysMaterial.Get()))
		{
			//Spec.CapturedTargetTags.GetSpecTags().AppendTags(PhysMatWithTags->Tags);
		}
	}
}


//检查某个能力是否满足释放的条件，如果不能释放，将会在OptionalRelevantTags（如果传入了有效指针的话）中存入代表失败理由的tag
bool UAsceGameplayAbility::DoesAbilitySatisfyTagRequirements(const UAbilitySystemComponent& AbilitySystemComponent, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
	// Specialized version to handle death exclusion and AbilityTags expansion via ASC
	

	bool bBlocked = false;//标记是否被某个tag阻止
	bool bMissing = false;//标记是否缺失了某个tag

	UAbilitySystemGlobals& AbilitySystemGlobals = UAbilitySystemGlobals::Get();
	
	//先获取这两个tag（标识该技能因为某个tag被阻止释放，或者因为缺少某个tag被阻止释放），之后用来标记状态
	const FGameplayTag& BlockedTag = AbilitySystemGlobals.ActivateFailTagsBlockedTag;
	const FGameplayTag& MissingTag = AbilitySystemGlobals.ActivateFailTagsMissingTag;

	// Check if any of this ability's tags are currently blocked
	// 检查当前的GA是否具有ASC所阻止的tag, 有的话直接就阻止释放
	if (AbilitySystemComponent.AreAbilityTagsBlocked(GetAssetTags()))
	{
		bBlocked = true;
	}

	const UAsceAbilitySystemComponent* AsceASC = Cast<UAsceAbilitySystemComponent>(&AbilitySystemComponent);
	static FGameplayTagContainer AllRequiredTags;
	static FGameplayTagContainer AllBlockedTags;

	//检查当前技能释放所需要的tag和被阻止的tag
	AllRequiredTags = ActivationRequiredTags;
	AllBlockedTags = ActivationBlockedTags;

	// Expand our ability tags to add additional required/blocked tags
	if (AsceASC)
	{
		AsceASC->GetAdditionalActivationTagRequirements(GetAssetTags(), AllRequiredTags, AllBlockedTags);
	}

	// Check to see the required/blocked tags for this ability
	// 检查当前希望释放技能的ASC是否拥有所有需要的Tag以及不拥有任何一个阻塞的Tag
	if (AllBlockedTags.Num() || AllRequiredTags.Num())
	{
		static FGameplayTagContainer AbilitySystemComponentTags;
		
		AbilitySystemComponentTags.Reset();
		AbilitySystemComponent.GetOwnedGameplayTags(AbilitySystemComponentTags);//获取当前ASC拥有的tag
		
		if (AbilitySystemComponentTags.HasAny(AllBlockedTags))//如果玩家死亡，必然通过这一步判断，此时可以进一步判断是否真的有死亡标签，并且将死亡作为理由返回
		{
			if (OptionalRelevantTags && AbilitySystemComponentTags.HasTag(AsceGameplayTags::Status_Death))
			{
				// If player is dead and was rejected due to blocking tags, give that feedback
				OptionalRelevantTags->AddTag(AsceGameplayTags::Ability_ActivateFail_IsDead);
			}

			bBlocked = true;
		}

		if (!AbilitySystemComponentTags.HasAll(AllRequiredTags))
		{
			bMissing = true;
		}
	}

	
	//来源标签，ASC身上的标签是第一道决定技能是否能释放的地方，然后仅凭ASC是不够的，比如玩家使用火系法杖，此时不能释放水系技能，或者某个水系技能需要水系法杖才能释放
	//此时如果直接在使用某个法杖的时候给ASC挂标签，并不合理，而是将释放技能的物品附带的标签作为SourceTag传入，技能不仅需要满足释放者具有某些前置条件，还要满足释放
	//技能使用的方法也得有某些前置条件
	if (SourceTags != nullptr)
	{
		if (SourceBlockedTags.Num() || SourceRequiredTags.Num())
		{
			if (SourceTags->HasAny(SourceBlockedTags))
			{
				bBlocked = true;
			}

			if (!SourceTags->HasAll(SourceRequiredTags))
			{
				bMissing = true;
			}
		}
	}

	
	//目标标签，这并不是目标ASC具有的完整标签，而仅仅只是一个快照，通常是一些概括性的标签，比如敌人，种族这种标签
	//用来快速决定技能是否能够释放，而技能能否真正作用于某个敌人时，此时会在应用Effect时检查敌人的ASC的标签
	//这样能进行快速的判断而不至于性能消耗过大。同时这也是符合逻辑的，大部分时候不需要对敌人释放技能时检查敌人身上是否有某种标签来决定能否释放
	//对于极少数情况可能需要这么做，此时也并非检查ASC，这样开销可能会比较大，而是附加这个Target标签(比如我想知道目标是否是敌人，在ASC的众多标签中查找太慢了)
	//Target标签通常在TargetData这种轻量化的数据中获取，TargetData通常存放目标的一些快照信息（种族，阵营之类的用于快速检查）
	if (TargetTags != nullptr)
	{
		if (TargetBlockedTags.Num() || TargetRequiredTags.Num())
		{
			if (TargetTags->HasAny(TargetBlockedTags))
			{
				bBlocked = true;
			}

			if (!TargetTags->HasAll(TargetRequiredTags))
			{
				bMissing = true;
			}
		}
	}

	//根据被阻止释放的原因来为相关tag容器中添加对应代表失败理由的tag(Blocked的优先级比Missing更高)
	if (bBlocked)
	{
		if (OptionalRelevantTags && BlockedTag.IsValid())
		{
			OptionalRelevantTags->AddTag(BlockedTag);
		}
		return false;
	}
	if (bMissing)
	{
		if (OptionalRelevantTags && MissingTag.IsValid())
		{
			OptionalRelevantTags->AddTag(MissingTag);
		}
		return false;
	}

	return true;
}

void UAsceGameplayAbility::OnPawnAvatarSet()
{
	K2_OnPawnAvatarSet();
}

void UAsceGameplayAbility::GetAbilitySource(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, float& OutSourceLevel, const IAsceAbilitySourceInterface*& OutAbilitySource, AActor*& OutEffectCauser) const
{
	//获取当前能力的来源(传入GASpecHandle, ActorInfo*, 返回来源等级，来源，造成者)
	//造成者是释放这个能力的角色，来源则是赋予造成者该能力的对象，比如枪赋予玩家射击能力，那么枪就是能力的来源
	OutSourceLevel = 0.0f;
	OutAbilitySource = nullptr;
	OutEffectCauser = nullptr;

	FGameplayEffectSpec Spec;
	
	OutEffectCauser = ActorInfo->AvatarActor.Get();
	// If we were added by something that's an ability info source, use it
	UObject* SourceObject = GetSourceObject(Handle, ActorInfo);

	OutAbilitySource = Cast<IAsceAbilitySourceInterface>(SourceObject);
}

void UAsceGameplayAbility::TryActivateAbilityOnSpawn(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) const
{
	// Try to activate if activation policy is on spawn.
	// 尝试激活，如果激活policy是在玩家生成的时候（Avatar被赋予的时候）激活
	if (ActorInfo && !Spec.IsActive() && (ActivationPolicy == EAsceAbilityActivationPolicy::OnSpawn))
	{
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		const AActor* AvatarActor = ActorInfo->AvatarActor.Get();

		// If avatar actor is torn off or about to die, don't try to activate until we get the new one.
		if (ASC && AvatarActor && !AvatarActor->GetTearOff() && (AvatarActor->GetLifeSpan() <= 0.0f))
		{
			const bool bIsLocalExecution = (NetExecutionPolicy == EGameplayAbilityNetExecutionPolicy::LocalPredicted) || (NetExecutionPolicy == EGameplayAbilityNetExecutionPolicy::LocalOnly);
			const bool bIsServerExecution = (NetExecutionPolicy == EGameplayAbilityNetExecutionPolicy::ServerOnly) || (NetExecutionPolicy == EGameplayAbilityNetExecutionPolicy::ServerInitiated);

			const bool bClientShouldActivate = ActorInfo->IsLocallyControlled() && bIsLocalExecution;
			const bool bServerShouldActivate = ActorInfo->IsNetAuthority() && bIsServerExecution;

			if (bClientShouldActivate || bServerShouldActivate)
			{
				ASC->TryActivateAbility(Spec.Handle);
			}
		}
	}
}

//此函数待删除，ActivationGroup已废弃
// bool UAsceGameplayAbility::CanChangeActivationGroup(EAsceAbilityActivationGroup NewGroup) const
// {
// 	//试想有一些技能同一时间只能释放一个，比如大部分的持续施法技能，在释放时变为Exclusive_Replaceable状态（独特且可替代），这代表同一时间
// 	//Exclusive类型的技能只能有一个在激活，Replaceable则是代表新尝试激活的技能如果其激活组为Exclusive，则会取代当前的Exclusive_Replaceable能力
// 	//而Exclusive_Blocking则代表此技能释放时，无法被其他技能取代，必须等待释放完成从这个状态中离开才能激活其他的Exclusive技能
// 	
// 	//检查是否能修改能力的激活组
// 	if (!IsInstantiated() || !IsActive())
// 	{
// 		return false;
// 	}
//
// 	if (ActivationGroup == NewGroup)
// 	{
// 		return true;
// 	}
//
// 	UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponentFromActorInfo();
// 	check(AsceASC);
//
// 	if ((ActivationGroup != EAsceAbilityActivationGroup::Exclusive_Blocking) && AsceASC->IsActivationGroupBlocked(NewGroup))
// 	{
// 		// This ability can't change groups if it's blocked (unless it is the one doing the blocking).
// 		// 如果这个独特能力的激活组不是阻塞其他独特能力，并且当前此能力希望转到的激活组会进入阻塞其他独特能力的状态，那么就不能转变为新的激活组
// 		return false;
// 	}
//
// 	if ((NewGroup == EAsceAbilityActivationGroup::Exclusive_Replaceable) && !CanBeCanceled())
// 	{
// 		// This ability can't become replaceable if it can't be canceled.
// 		// 这个能力希望转变为Exclusive_Replaceable激活组，但是这个能力是不能被取消的，那就不能转变为此激活组
// 		return false;
// 	}
// 	
// 	return true;
// }

//ActivationGroup废弃了，这个函数随后删除
// bool UAsceGameplayAbility::ChangeActivationGroup(EAsceAbilityActivationGroup NewGroup)
// {
// 	//检测尝试转变此能力的激活组，此能力必须是Instanced的
// 	ENSURE_ABILITY_IS_INSTANTIATED_OR_RETURN(ChangeActivationGroup, false);
//
// 	if (!CanChangeActivationGroup(NewGroup))
// 	{
// 		return false;
// 	}
//
// 	if (ActivationGroup != NewGroup)
// 	{
// 		UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponentFromActorInfo();
// 		check(AsceASC);
//
// 		AsceASC->RemoveAbilityFromActivationGroup(ActivationGroup, this);
// 		AsceASC->AddAbilityToActivationGroup(NewGroup, this);
//
// 		ActivationGroup = NewGroup;
// 	}
//
// 	return true;
// }

void UAsceGameplayAbility::SetCameraMode(TSubclassOf<UAsceCameraMode> CameraMode)
{
	ENSURE_ABILITY_IS_INSTANTIATED_OR_RETURN(SetCameraMode, );

	if (UAsceHeroComponent* HeroComponent = GetHeroComponentFromActorInfo())
	{
		HeroComponent->SetAbilityCameraMode(CameraMode, CurrentSpecHandle);
		ActiveCameraMode = CameraMode;
	}
}

void UAsceGameplayAbility::ClearCameraMode()
{
	ENSURE_ABILITY_IS_INSTANTIATED_OR_RETURN(ClearCameraMode, );

	if (ActiveCameraMode)
	{
		if (UAsceHeroComponent* HeroComponent = GetHeroComponentFromActorInfo())
		{
			HeroComponent->ClearAbilityCameraMode(CurrentSpecHandle);
		}

		ActiveCameraMode = nullptr;
	}
}

