// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsceCharacter.h"

#include "AbilitySystem/AsceAbilitySystemComponent.h"
#include "Camera/AsceCameraComponent.h"
#include "Character/AsceHealthComponent.h"
#include "Character/AscePawnExtensionComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "AsceCharacterMovementComponent.h"
#include "AsceGameplayTags.h"
#include "AsceLogChannels.h"
#include "Net/UnrealNetwork.h"
#include "Player/AscePlayerController.h"
#include "Player/AscePlayerState.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceCharacter)

class AActor;
class FLifetimeProperty;
class IRepChangedPropertyTracker;
class UInputComponent;


//角色碰撞配置名称, 设置到Capsule和Mesh上，这两个组件将会去读取相关的配置来设置自己的碰撞
static FName NAME_AsceCharacterCollisionProfile_Capsule(TEXT("AscePawnCapsule"));
static FName NAME_AsceCharacterCollisionProfile_Mesh(TEXT("AscePawnMesh"));

AAsceCharacter::AAsceCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UAsceCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))//重新指定要使用的移动组件
{
	// Avoid ticking characters if possible.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SetNetCullDistanceSquared(900000000.0f);

	UCapsuleComponent* CapsuleComp = GetCapsuleComponent();
	check(CapsuleComp);
	CapsuleComp->InitCapsuleSize(40.0f, 90.0f);
	CapsuleComp->SetCollisionProfileName(NAME_AsceCharacterCollisionProfile_Capsule);

	USkeletalMeshComponent* MeshComp = GetMesh();
	check(MeshComp);
	MeshComp->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));  // Rotate mesh to be X forward since it is exported as Y forward.
	MeshComp->SetCollisionProfileName(NAME_AsceCharacterCollisionProfile_Mesh);

	UAsceCharacterMovementComponent* AsceMoveComp = CastChecked<UAsceCharacterMovementComponent>(GetCharacterMovement());
	AsceMoveComp->GravityScale = 1.0f;
	AsceMoveComp->MaxAcceleration = 2400.0f;
	AsceMoveComp->BrakingFrictionFactor = 1.0f;
	AsceMoveComp->BrakingFriction = 6.0f;
	AsceMoveComp->GroundFriction = 8.0f;
	AsceMoveComp->BrakingDecelerationWalking = 1400.0f;
	AsceMoveComp->bUseControllerDesiredRotation = false;
	AsceMoveComp->bOrientRotationToMovement = false;
	AsceMoveComp->RotationRate = FRotator(0.0f, 720.0f, 0.0f);
	AsceMoveComp->bAllowPhysicsRotationDuringAnimRootMotion = false;
	AsceMoveComp->GetNavAgentPropertiesRef().bCanCrouch = true;
	AsceMoveComp->bCanWalkOffLedgesWhenCrouching = true;
	AsceMoveComp->SetCrouchedHalfHeight(65.0f);

	PawnExtComponent = CreateDefaultSubobject<UAscePawnExtensionComponent>(TEXT("PawnExtensionComponent"));
	//创建ASC初始化完成的回调
	PawnExtComponent->OnAbilitySystemInitialized_RegisterAndCall(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnAbilitySystemInitialized));
	//创建ASC反初始化的回调
	PawnExtComponent->OnAbilitySystemUninitialized_Register(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnAbilitySystemUninitialized));

	HealthComponent = CreateDefaultSubobject<UAsceHealthComponent>(TEXT("HealthComponent"));
	HealthComponent->OnDeathStarted.AddDynamic(this, &ThisClass::OnDeathStarted);
	HealthComponent->OnDeathFinished.AddDynamic(this, &ThisClass::OnDeathFinished);

	CameraComponent = CreateDefaultSubobject<UAsceCameraComponent>(TEXT("CameraComponent"));
	CameraComponent->SetRelativeLocation(FVector(-300.0f, 0.0f, 75.0f));

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	BaseEyeHeight = 80.0f;
	CrouchedEyeHeight = 50.0f;
}

void AAsceCharacter::PreInitializeComponents()
{
	Super::PreInitializeComponents();
}

void AAsceCharacter::BeginPlay()
{
	Super::BeginPlay();
}

void AAsceCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void AAsceCharacter::Reset()
{
	DisableMovementAndCollision();

	K2_OnReset();

	UninitAndDestroy();
}

void AAsceCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ThisClass, ReplicatedAcceleration, COND_SimulatedOnly);
	DOREPLIFETIME(ThisClass, MyTeamID)
}

void AAsceCharacter::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	//此函数在服务器上网络复制之前调用，在主控代理记录回放的时候也会调用
	Super::PreReplication(ChangedPropertyTracker);

	//每个主控代理在其他客户端上都是模拟代理，因此这里的计算是必须的
	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		// Compress Acceleration: XY components as direction + magnitude, Z component as direct value
		const double MaxAccel = MovementComponent->MaxAcceleration;
		const FVector CurrentAccel = MovementComponent->GetCurrentAcceleration();
		double AccelXYRadians, AccelXYMagnitude;
		FMath::CartesianToPolar(CurrentAccel.X, CurrentAccel.Y, AccelXYMagnitude, AccelXYRadians);

		ReplicatedAcceleration.AccelXYRadians   = FMath::FloorToInt((AccelXYRadians / TWO_PI) * 255.0);     // [0, 2PI] -> [0, 255]
		ReplicatedAcceleration.AccelXYMagnitude = FMath::FloorToInt((AccelXYMagnitude / MaxAccel) * 255.0);	// [0, MaxAccel] -> [0, 255]
		ReplicatedAcceleration.AccelZ           = FMath::FloorToInt((CurrentAccel.Z / MaxAccel) * 127.0);   // [-MaxAccel, MaxAccel] -> [-127, 127]
	}
}

void AAsceCharacter::NotifyControllerChanged()
{
	const FGenericTeamId OldTeamId = GetGenericTeamId();

	Super::NotifyControllerChanged();

	// Update our team ID based on the controller
	if (HasAuthority() && (GetController() != nullptr))
	{
		if (IAsceTeamAgentInterface* ControllerWithTeam = Cast<IAsceTeamAgentInterface>(GetController()))
		{
			MyTeamID = ControllerWithTeam->GetGenericTeamId();
			ConditionalBroadcastTeamChanged(this, OldTeamId, MyTeamID);
		}
	}
}

AAscePlayerController* AAsceCharacter::GetAscePlayerController() const
{
	return CastChecked<AAscePlayerController>(GetController(), ECastCheckedType::NullAllowed);
}

AAscePlayerState* AAsceCharacter::GetAscePlayerState() const
{
	return CastChecked<AAscePlayerState>(GetPlayerState(), ECastCheckedType::NullAllowed);
}

UAsceAbilitySystemComponent* AAsceCharacter::GetAsceAbilitySystemComponent() const
{
	return Cast<UAsceAbilitySystemComponent>(GetAbilitySystemComponent());
}

UAbilitySystemComponent* AAsceCharacter::GetAbilitySystemComponent() const
{
	if (PawnExtComponent == nullptr)
	{
		return nullptr;
	}

	return PawnExtComponent->GetAsceAbilitySystemComponent();
}

void AAsceCharacter::OnAbilitySystemInitialized()
{
	//PawnExtensionComponent获取到ASC时回调此函数, 此时初始化HealthComponent
	UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponent();
	check(AsceASC);

	HealthComponent->InitializeWithAbilitySystem(AsceASC);

	InitializeGameplayTags();
}

void AAsceCharacter::OnAbilitySystemUninitialized()
{
	HealthComponent->UninitializeFromAbilitySystem();
}

void AAsceCharacter::PossessedBy(AController* NewController)
{
	const FGenericTeamId OldTeamID = MyTeamID;

	Super::PossessedBy(NewController);

	//让PawnExtComponent处理Controller改变(包括让ASC刷一下ActorInfo，尝试初始化状态转移)
	PawnExtComponent->HandleControllerChanged();

	// 绑定队伍改变的回调
	// Grab the current team ID and listen for future changes
	if (IAsceTeamAgentInterface* ControllerAsTeamProvider = Cast<IAsceTeamAgentInterface>(NewController))
	{
		MyTeamID = ControllerAsTeamProvider->GetGenericTeamId();
		ControllerAsTeamProvider->GetTeamChangedDelegateChecked().AddDynamic(this, &ThisClass::OnControllerChangedTeam);
	}
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
}

void AAsceCharacter::UnPossessed()
{
	AController* const OldController = GetController();

	//移除队伍改变回调
	// Stop listening for changes from the old controller
	const FGenericTeamId OldTeamID = MyTeamID;
	if (IAsceTeamAgentInterface* ControllerAsTeamProvider = Cast<IAsceTeamAgentInterface>(OldController))
	{
		ControllerAsTeamProvider->GetTeamChangedDelegateChecked().RemoveAll(this);
	}

	Super::UnPossessed();

	PawnExtComponent->HandleControllerChanged();

	// Determine what the new team ID should be afterwards
	// 决定不再被Conroller占有后的队伍ID
	MyTeamID = DetermineNewTeamAfterPossessionEnds(OldTeamID);
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
}

void AAsceCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();
	
	//在客户端上，Controller被复制过来的时候也触发一下PawnExtComponent的回调
	//客户端上不需要处理队伍信息，因此这里不用管
	PawnExtComponent->HandleControllerChanged();
}

void AAsceCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	PawnExtComponent->HandlePlayerStateReplicated();
}

void AAsceCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	
	//实际上只尝试了一下初始化状态转移
	//在HeroComponent中进行真正的输入映射绑定
	PawnExtComponent->SetupPlayerInputComponent();
}

void AAsceCharacter::InitializeGameplayTags()
{
	// Clear tags that may be lingering on the ability system from the previous pawn.
	// 先清理一遍ASC上残留的tag
	if (UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponent())
	{
		for (const TPair<uint8, FGameplayTag>& TagMapping : AsceGameplayTags::MovementModeTagMap)
		{
			if (TagMapping.Value.IsValid())
			{
				AsceASC->SetLooseGameplayTagCount(TagMapping.Value, 0);
			}
		}

		for (const TPair<uint8, FGameplayTag>& TagMapping : AsceGameplayTags::CustomMovementModeTagMap)
		{
			if (TagMapping.Value.IsValid())
			{
				AsceASC->SetLooseGameplayTagCount(TagMapping.Value, 0);
			}
		}

		UAsceCharacterMovementComponent* AsceMoveComp = CastChecked<UAsceCharacterMovementComponent>(GetCharacterMovement());
		SetMovementModeTag(AsceMoveComp->MovementMode, AsceMoveComp->CustomMovementMode, true);
	}
}

void AAsceCharacter::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	if (const UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponent())
	{
		AsceASC->GetOwnedGameplayTags(TagContainer);
	}
}

bool AAsceCharacter::HasMatchingGameplayTag(FGameplayTag TagToCheck) const
{
	if (const UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponent())
	{
		return AsceASC->HasMatchingGameplayTag(TagToCheck);
	}

	return false;
}

bool AAsceCharacter::HasAllMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const
{
	if (const UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponent())
	{
		return AsceASC->HasAllMatchingGameplayTags(TagContainer);
	}

	return false;
}

bool AAsceCharacter::HasAnyMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const
{
	if (const UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponent())
	{
		return AsceASC->HasAnyMatchingGameplayTags(TagContainer);
	}

	return false;
}

void AAsceCharacter::FellOutOfWorld(const class UDamageType& dmgType)
{
	//掉出世界
	HealthComponent->DamageSelfDestruct(/*bFellOutOfWorld=*/ true);
}

void AAsceCharacter::OnDeathStarted(AActor*)
{
	DisableMovementAndCollision();
}

void AAsceCharacter::OnDeathFinished(AActor*)
{
	//下一帧销毁
	GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::DestroyDueToDeath);
}


void AAsceCharacter::DisableMovementAndCollision()
{
	if (GetController())
	{
		GetController()->SetIgnoreMoveInput(true);
	}

	UCapsuleComponent* CapsuleComp = GetCapsuleComponent();
	check(CapsuleComp);
	CapsuleComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CapsuleComp->SetCollisionResponseToAllChannels(ECR_Ignore);

	UAsceCharacterMovementComponent* AsceMoveComp = CastChecked<UAsceCharacterMovementComponent>(GetCharacterMovement());
	AsceMoveComp->StopMovementImmediately();
	AsceMoveComp->DisableMovement();
}

void AAsceCharacter::DestroyDueToDeath()
{
	K2_OnDeathFinished();

	UninitAndDestroy();
}


void AAsceCharacter::UninitAndDestroy()
{
	if (GetLocalRole() == ROLE_Authority)
	{
		DetachFromControllerPendingDestroy();
		SetLifeSpan(0.1f);
	}

	// Uninitialize the ASC if we're still the avatar actor (otherwise another pawn already did it when they became the avatar actor)
	if (UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponent())
	{
		if (AsceASC->GetAvatarActor() == this)
		{
			PawnExtComponent->UninitializeAbilitySystem();
		}
	}

	SetActorHiddenInGame(true);
}

void AAsceCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	UAsceCharacterMovementComponent* AsceMoveComp = CastChecked<UAsceCharacterMovementComponent>(GetCharacterMovement());

	SetMovementModeTag(PrevMovementMode, PreviousCustomMode, false);
	SetMovementModeTag(AsceMoveComp->MovementMode, AsceMoveComp->CustomMovementMode, true);
}

void AAsceCharacter::SetMovementModeTag(EMovementMode MovementMode, uint8 CustomMovementMode, bool bTagEnabled)
{
	if (UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponent())
	{
		const FGameplayTag* MovementModeTag = nullptr;
		if (MovementMode == MOVE_Custom)
		{
			MovementModeTag = AsceGameplayTags::CustomMovementModeTagMap.Find(CustomMovementMode);
		}
		else
		{
			MovementModeTag = AsceGameplayTags::MovementModeTagMap.Find(MovementMode);
		}

		if (MovementModeTag && MovementModeTag->IsValid())
		{
			AsceASC->SetLooseGameplayTagCount(*MovementModeTag, (bTagEnabled ? 1 : 0));
		}
	}
}

void AAsceCharacter::ToggleCrouch()
{
	const UAsceCharacterMovementComponent* LyraMoveComp = CastChecked<UAsceCharacterMovementComponent>(GetCharacterMovement());

	if (IsCrouched() || LyraMoveComp->bWantsToCrouch)
	{
		UnCrouch();
	}
	else if (LyraMoveComp->IsMovingOnGround())
	{
		Crouch();
	}
}

void AAsceCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	if (UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponent())
	{
		AsceASC->SetLooseGameplayTagCount(AsceGameplayTags::Status_Crouching, 1);
	}


	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
}

void AAsceCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	if (UAsceAbilitySystemComponent* AsceASC = GetAsceAbilitySystemComponent())
	{
		AsceASC->SetLooseGameplayTagCount(AsceGameplayTags::Status_Crouching, 0);
	}

	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
}

bool AAsceCharacter::CanJumpInternal_Implementation() const
{
	// same as ACharacter's implementation but without the crouch check
	return JumpIsAllowedInternal();
}

void AAsceCharacter::OnRep_ReplicatedAcceleration()
{
	if (UAsceCharacterMovementComponent* AsceMovementComponent = Cast<UAsceCharacterMovementComponent>(GetCharacterMovement()))
	{
		// Decompress Acceleration
		// 解算同步过来的加速度  FVector 24字节，使用这种方式打包只需要3字节
		const double MaxAccel         = AsceMovementComponent->MaxAcceleration;
		const double AccelXYMagnitude = double(ReplicatedAcceleration.AccelXYMagnitude) * MaxAccel / 255.0; // [0, 255] -> [0, MaxAccel]
		const double AccelXYRadians   = double(ReplicatedAcceleration.AccelXYRadians) * TWO_PI / 255.0;     // [0, 255] -> [0, 2PI]

		FVector UnpackedAcceleration(FVector::ZeroVector);
		FMath::PolarToCartesian(AccelXYMagnitude, AccelXYRadians, UnpackedAcceleration.X, UnpackedAcceleration.Y);
		UnpackedAcceleration.Z = double(ReplicatedAcceleration.AccelZ) * MaxAccel / 127.0; // [-127, 127] -> [-MaxAccel, MaxAccel]

		AsceMovementComponent->SetReplicatedAcceleration(UnpackedAcceleration);
	}
}

void AAsceCharacter::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	if (GetController() == nullptr)
	{
		if (HasAuthority())
		{
			const FGenericTeamId OldTeamID = MyTeamID;
			MyTeamID = NewTeamID;
			ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
		}
		else
		{
			UE_LOG(LogAsceTeams, Error, TEXT("You can't set the team ID on a character (%s) except on the authority"), *GetPathNameSafe(this));
		}
	}
	else
	{
		UE_LOG(LogAsceTeams, Error, TEXT("You can't set the team ID on a possessed character (%s); it's driven by the associated controller"), *GetPathNameSafe(this));
	}
}

FGenericTeamId AAsceCharacter::GetGenericTeamId() const
{
	return MyTeamID;
}

FOnAsceTeamIndexChangedDelegate* AAsceCharacter::GetOnTeamIndexChangedDelegate()
{
	return &OnTeamChangedDelegate;
}

void AAsceCharacter::OnControllerChangedTeam(UObject* TeamAgent, int32 OldTeam, int32 NewTeam)
{
	const FGenericTeamId MyOldTeamID = MyTeamID;
	MyTeamID = IntegerToGenericTeamId(NewTeam);
	ConditionalBroadcastTeamChanged(this, MyOldTeamID, MyTeamID);
}

void AAsceCharacter::OnRep_MyTeamID(FGenericTeamId OldTeamID)
{
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
}

bool AAsceCharacter::UpdateSharedReplication()
{
	if (GetLocalRole() == ROLE_Authority)
	{
		FSharedRepMovement SharedMovement;
		if (SharedMovement.FillForCharacter(this))
		{
			// Only call FastSharedReplication if data has changed since the last frame.
			// Skipping this call will cause replication to reuse the same bunch that we previously
			// produced, but not send it to clients that already received. (But a new client who has not received
			// it, will get it this frame)
			//只会在数据相对于上一帧改变的时候调用FastShared复制，不会把旧的数据发给客户端(因为没有改变就不会触发复制)，但是没有收到这个数据的客户端（新加入的）还是会收到这个数据
			if (!SharedMovement.Equals(LastSharedReplication, this))
			{
				LastSharedReplication = SharedMovement;
				SetReplicatedMovementMode(SharedMovement.RepMovementMode);

				FastSharedReplication(SharedMovement);
			}
			return true;
		}
	}

	// We cannot fastrep right now. Don't send anything.
	return false;
}

void AAsceCharacter::FastSharedReplication_Implementation(const FSharedRepMovement& SharedRepMovement)
{
	if (GetWorld()->IsPlayingReplay())
	{
		return;
	}

	// Timestamp is checked to reject old moves.
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		// Timestamp
		SetReplicatedServerLastTransformUpdateTimeStamp(SharedRepMovement.RepTimeStamp);

		// Movement mode
		if (GetReplicatedMovementMode() != SharedRepMovement.RepMovementMode)
		{
			SetReplicatedMovementMode(SharedRepMovement.RepMovementMode);
			GetCharacterMovement()->bNetworkMovementModeChanged = true;
			GetCharacterMovement()->bNetworkUpdateReceived = true;
		}

		// Location, Rotation, Velocity, etc.
		FRepMovement& MutableRepMovement = GetReplicatedMovement_Mutable();
		MutableRepMovement = SharedRepMovement.RepMovement;

		// This also sets LastRepMovement
		OnRep_ReplicatedMovement();

		// Jump force
		SetProxyIsJumpForceApplied(SharedRepMovement.bProxyIsJumpForceApplied);

		// Crouch
		if (IsCrouched() != SharedRepMovement.bIsCrouched)
		{
			SetIsCrouched(SharedRepMovement.bIsCrouched);
			OnRep_IsCrouched();
		}
	}
}

FSharedRepMovement::FSharedRepMovement()
{
	RepMovement.LocationQuantizationLevel = EVectorQuantization::RoundTwoDecimals;
}

bool FSharedRepMovement::FillForCharacter(ACharacter* Character)
{
	if (USceneComponent* PawnRootComponent = Character->GetRootComponent())
	{
		UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement();

		RepMovement.Location = FRepMovement::RebaseOntoZeroOrigin(PawnRootComponent->GetComponentLocation(), Character);
		RepMovement.Rotation = PawnRootComponent->GetComponentRotation();
		RepMovement.LinearVelocity = CharacterMovement->Velocity;
		RepMovementMode = CharacterMovement->PackNetworkMovementMode();
		bProxyIsJumpForceApplied = Character->GetProxyIsJumpForceApplied() || (Character->JumpForceTimeRemaining > 0.0f);
		bIsCrouched = Character->IsCrouched();

		// Timestamp is sent as zero if unused
		if ((CharacterMovement->NetworkSmoothingMode == ENetworkSmoothingMode::Linear) || CharacterMovement->bNetworkAlwaysReplicateTransformUpdateTimestamp)
		{
			RepTimeStamp = CharacterMovement->GetServerLastTransformUpdateTimeStamp();
		}
		else
		{
			RepTimeStamp = 0.f;
		}

		return true;
	}
	return false;
}

bool FSharedRepMovement::Equals(const FSharedRepMovement& Other, ACharacter* Character) const
{
	if (RepMovement.Location != Other.RepMovement.Location)
	{
		return false;
	}

	if (RepMovement.Rotation != Other.RepMovement.Rotation)
	{
		return false;
	}

	if (RepMovement.LinearVelocity != Other.RepMovement.LinearVelocity)
	{
		return false;
	}

	if (RepMovementMode != Other.RepMovementMode)
	{
		return false;
	}

	if (bProxyIsJumpForceApplied != Other.bProxyIsJumpForceApplied)
	{
		return false;
	}

	if (bIsCrouched != Other.bIsCrouched)
	{
		return false;
	}

	return true;
}

bool FSharedRepMovement::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	bOutSuccess = true;
	RepMovement.NetSerialize(Ar, Map, bOutSuccess);
	Ar << RepMovementMode;
	Ar << bProxyIsJumpForceApplied;
	Ar << bIsCrouched;

	// Timestamp, if non-zero.
	uint8 bHasTimeStamp = (RepTimeStamp != 0.f);
	Ar.SerializeBits(&bHasTimeStamp, 1);
	if (bHasTimeStamp)
	{
		Ar << RepTimeStamp;
	}
	else
	{
		RepTimeStamp = 0.f;
	}

	return true;
}