// Fill out your copyright notice in the Description page of Project Settings.


#include "AscePlayerState.h"
#include "AbilitySystem/Attributes/AsceCombatSet.h"
#include "AbilitySystem/Attributes/AsceHealthSet.h"
#include "AbilitySystem/AsceAbilitySet.h"
#include "AbilitySystem/AsceAbilitySystemComponent.h"
#include "Character/AscePawnData.h"
#include "Character/AscePawnExtensionComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/AsceExperienceManagerComponent.h"
//@TODO: Would like to isolate this a bit better to get the pawn data in here without this having to know about other stuff
#include "GameModes/AsceGameMode.h"
#include "GameModes/AsceGameState.h"
#include "AsceMass/Vision/AsceFogRenderSubsystem.h"
#include "AsceLogChannels.h"
#include "AscePlayerController.h"
#include "Messages/AsceVerbMessage.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AscePlayerState)

class AController;
class APlayerState;
class FLifetimeProperty;

const FName AAscePlayerState::NAME_AsceAbilityReady("AsceAbilitiesReady");

AAscePlayerState::AAscePlayerState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MyPlayerConnectionType(EAscePlayerConnectionType::Player)
{
	//把AbilitySystem创建到PlayerState上
	AbilitySystemComponent = ObjectInitializer.CreateDefaultSubobject<UAsceAbilitySystemComponent>(this, TEXT("AbilitySystemComponent"));
	
	//创建能力组件时设置一下复制，通常情况下，总是采用Mixed复制方式， 会将对应玩家的（自主代理）能力组件的全部信息复制到客户端，而其他玩家的（模拟代理）能力组件信息只同步必要部分。
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// These attribute sets will be detected by AbilitySystemComponent::InitializeComponent. Keeping a reference so that the sets don't get garbage collected before that.
	// 创建属性集并且保持引用来避免垃圾回收
	HealthSet = CreateDefaultSubobject<UAsceHealthSet>(TEXT("HealthSet"));
	CombatSet = CreateDefaultSubobject<UAsceCombatSet>(TEXT("CombatSet"));

	// AbilitySystemComponent needs to be updated at a high frequency.
	// 同步是以Actor为单位的，因此此处设置playerstate的同步频率来保证能力组件的高频更新
	SetNetUpdateFrequency(100.0f);

	
	//队伍ID和小队ID
	MyTeamID = FGenericTeamId::NoTeam;
	MySquadID = INDEX_NONE;
}

void AAscePlayerState::PreInitializeComponents()
{
	Super::PreInitializeComponents();
}

void AAscePlayerState::Reset()
{
	//重置PlayerState的数据，会调用Actor的Reset并且进行一次网络更新同步到客户端
	//由于继承自ModularActor，还会调用身上所有的Component的Reset
	//只在服务器上执行
	Super::Reset();
}

void AAscePlayerState::ClientInitialize(AController* C)
{
	//服务器会把PlayerState复制到客户端，PlayerController存有PlayerState的引用，首次复制到客户端时
	//PlayerController会调用此函数
	Super::ClientInitialize(C);

	if (UAscePawnExtensionComponent* PawnExtComp = UAscePawnExtensionComponent::FindPawnExtensionComponent(GetPawn()))
	{
		PawnExtComp->CheckDefaultInitialization();
	}
}

void AAscePlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);
	//服务器调用，此时玩家断开连接，需要复制并保存属性
	//@TODO: Copy stats
}

void AAscePlayerState::OnDeactivated()
{
	bool bDestroyDeactivatedPlayerState = false;

	switch (GetPlayerConnectionType())
	{
		case EAscePlayerConnectionType::Player:
		case EAscePlayerConnectionType::InactivePlayer:
			//@TODO: Ask the experience if we should destroy disconnecting players immediately or leave them around
			//可以在此处检查Experience是否需要销毁断开连接的玩家, 此处直接销毁了
			// (e.g., for long running servers where they might build up if lots of players cycle through)
			bDestroyDeactivatedPlayerState = true;
			break;
		default:
			bDestroyDeactivatedPlayerState = true;
			break;
	}
	
	SetPlayerConnectionType(EAscePlayerConnectionType::InactivePlayer);

	if (bDestroyDeactivatedPlayerState)
	{
		Destroy();
	}
}

void AAscePlayerState::OnReactivated()
{
	if (GetPlayerConnectionType() == EAscePlayerConnectionType::InactivePlayer)
	{
		SetPlayerConnectionType(EAscePlayerConnectionType::Player);
	}
}

void AAscePlayerState::OnExperienceLoaded(const UAsceExperienceDefinition* /*CurrentExperience*/)
{
	//Experience加载完成时的回调，设置PawnData到PlayerState上
	//只有服务器注册了此回调，因此只有在服务器上会调用，客户端上的PawnData是复制过去的
	if (AAsceGameMode* AsceGameMode = GetWorld()->GetAuthGameMode<AAsceGameMode>())
	{
		//这里的AsceGameMode可能会空
		if (const UAscePawnData* NewPawnData = AsceGameMode->GetPawnDataForController(GetOwningController()))
		{
			SetPawnData(NewPawnData);
		}
		else
		{
			//正常来讲，只会在Experience没有加载完成时PanwData才会为nullptr，但是此函数是Expiernce加载完成的回调
			UE_LOG(LogAsce, Error, TEXT("AAscePlayerState::OnExperienceLoaded(): Unable to find PawnData to initialize player state [%s]!"), *GetNameSafe(this));
		}
	}
}

void AAscePlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	//设置网络复制相关的配置
	
	//使用PushModel, 当此变量改变时，需要手动调用函数来标记此变量变化以及需要复制到客户端，大大减轻了网络复制的检查开销
	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;

	//这些变量使用PushModel同步，没有额外条件
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, PawnData, SharedParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, MyPlayerConnectionType, SharedParams)
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, MyTeamID, SharedParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, MySquadID, SharedParams);

	//ViewRotation不仅使用PushModel，并且只同步给Owner(PlayerController)以外的客户端（即玩家本身的客户端不会同步此值, 客户端通过rpc来修改服务器上的此值，服务器再同步给其他客户端，因此此值owner所在的客户端不需要再同步，否则会循环）
	SharedParams.Condition = ELifetimeCondition::COND_SkipOwner;
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, ReplicatedViewRotation, SharedParams);

	//StatTags无条件同步
	DOREPLIFETIME(ThisClass, StatTags);	
}

FRotator AAscePlayerState::GetReplicatedViewRotation() const
{
	// Could replace this with custom replication
	return ReplicatedViewRotation;
}

void AAscePlayerState::SetReplicatedViewRotation(const FRotator& NewRotation)
{
	if (NewRotation != ReplicatedViewRotation)
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, ReplicatedViewRotation, this);
		ReplicatedViewRotation = NewRotation;
	}
}

AAscePlayerController* AAscePlayerState::GetAscePlayerController() const
{
	return Cast<AAscePlayerController>(GetOwner());
}

UAbilitySystemComponent* AAscePlayerState::GetAbilitySystemComponent() const
{
	return GetAsceAbilitySystemComponent();
}

void AAscePlayerState::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	check(AbilitySystemComponent);
	//Owner设置为PlayerState， Avatar为PlayerPanw（或许把Owner设置为Controller，并且把ASC放到Controller上更好？）
	AbilitySystemComponent->InitAbilityActorInfo(this, GetPawn());

	
	UWorld* World = GetWorld();
	if (World && World->IsGameWorld() && World->GetNetMode() != NM_Client)
	{
		//服务端调用, 注册Experience完成加载时的回调
		AGameStateBase* GameState = GetWorld()->GetGameState();
		check(GameState);
		UAsceExperienceManagerComponent* ExperienceComponent = GameState->FindComponentByClass<UAsceExperienceManagerComponent>();
		check(ExperienceComponent);
		ExperienceComponent->CallOrRegister_OnExperienceLoaded(FOnAsceExperienceLoaded::FDelegate::CreateUObject(this, &ThisClass::OnExperienceLoaded));
	}
}

void AAscePlayerState::SetPawnData(const UAscePawnData* InPawnData)
{
	check(InPawnData);

	if (GetLocalRole() != ROLE_Authority)// Actor在创建自己的GameInstance上为Authority，PlayerState由服务器创建，复制到客户端。所以此处仅仅服务器能通过
	{
		return;
	}

	if (PawnData)
	{
		//已有PawnData，此时不能重新设置了，因为会给能力组件授予PanwData中的能力
		UE_LOG(LogAsce, Error, TEXT("Trying to set PawnData [%s] on player state [%s] that already has valid PawnData [%s]."), *GetNameSafe(InPawnData), *GetNameSafe(this), *GetNameSafe(PawnData));
		return;
	}

	MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, PawnData, this);
	PawnData = InPawnData;

	for (const UAsceAbilitySet* AbilitySet : PawnData->AbilitySets)
	{
		if (AbilitySet)
		{
			AbilitySet->GiveToAbilitySystem(AbilitySystemComponent, nullptr);
		}
	}

	UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(this, NAME_AsceAbilityReady);
	
	ForceNetUpdate();
}

void AAscePlayerState::OnRep_PawnData()
{
}

void AAscePlayerState::SetPlayerConnectionType(EAscePlayerConnectionType NewType)
{
	MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, MyPlayerConnectionType, this);
	MyPlayerConnectionType = NewType;
}

void AAscePlayerState::SetSquadID(int32 NewSquadId)
{
	if (HasAuthority())
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, MySquadID, this);

		MySquadID = NewSquadId;
	}
}

void AAscePlayerState::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	if (HasAuthority())
	{
		const FGenericTeamId OldTeamID = MyTeamID;

		MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, MyTeamID, this);
		MyTeamID = NewTeamID;
		ConditionalBroadcastTeamChanged(this, OldTeamID, NewTeamID);
		RefreshLocalFogTeamKey();
	}
	else
	{
		UE_LOG(LogAsceTeams, Error, TEXT("Cannot set team for %s on non-authority"), *GetPathName(this));
	}
}

FGenericTeamId AAscePlayerState::GetGenericTeamId() const
{
	return MyTeamID;
}

FOnAsceTeamIndexChangedDelegate* AAscePlayerState::GetOnTeamIndexChangedDelegate()
{
	return &OnTeamChangedDelegate;
}

void AAscePlayerState::OnRep_MyTeamID(FGenericTeamId OldTeamID)
{
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
	RefreshLocalFogTeamKey();
}

void AAscePlayerState::RefreshLocalFogTeamKey()
{
	// 本端阵营表登记：客户端也要（否则本地 index 解析全部落在 0 列/行）
	AAsceGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AAsceGameState>() : nullptr;
	if (GameState)
	{
		GameState->GetOrAssignTeamIndex(GenericTeamIdToInteger(MyTeamID));
	}

	// 黑雾渲染只关心**本地玩家**的队伍行：远端 PlayerState 复制过来也会走这里，必须按本地控制器过滤
	const APlayerController* PC = Cast<APlayerController>(GetOwningController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	if (UAsceFogRenderSubsystem* FogRender = GetWorld() ? GetWorld()->GetSubsystem<UAsceFogRenderSubsystem>() : nullptr)
	{
		const int32 PackedTeamKey = GameState ? GameState->GetTeamKey(GenericTeamIdToInteger(MyTeamID)) : 0;
		FogRender->SetLocalTeamKey(PackedTeamKey);
	}
}

void AAscePlayerState::OnRep_MySquadID()
{
	//@TODO: Let the squad subsystem know (once that exists)
}

void AAscePlayerState::AddStatTagStack(FGameplayTag Tag, int32 StackCount)
{
	//加状态标签
	StatTags.AddStack(Tag, StackCount);
}

void AAscePlayerState::RemoveStatTagStack(FGameplayTag Tag, int32 StackCount)
{
	//移除状态标签
	StatTags.RemoveStack(Tag, StackCount);
}

int32 AAscePlayerState::GetStatTagStackCount(FGameplayTag Tag) const
{
	//获取状态标签数目
	return StatTags.GetStackCount(Tag);
}

bool AAscePlayerState::HasStatTag(FGameplayTag Tag) const
{
	return StatTags.ContainsTag(Tag);
}

void AAscePlayerState::ClientBroadcastMessage_Implementation(const FAsceVerbMessage Message)
{
	// This check is needed to prevent running the action when in standalone mode
	if (GetNetMode() == NM_Client)
	{
		UGameplayMessageSubsystem::Get(this).BroadcastMessage(Message.Verb, Message); 
	}
}

