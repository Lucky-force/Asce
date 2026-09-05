
#include "AsceHeroComponent.h"
#include "Components/GameFrameworkComponentDelegates.h"
#include "Logging/MessageLog.h"
#include "AsceLogChannels.h"
#include "EnhancedInputSubsystems.h"
#include "Player/AscePlayerController.h"
#include "Player/AscePlayerState.h"
#include "Player/AsceLocalPlayer.h"
#include "Character/AscePawnExtensionComponent.h"
#include "Character/AscePawnData.h"
#include "Character/AsceCharacter.h"
#include "AbilitySystem/AsceAbilitySystemComponent.h"
#include "Input/AsceInputConfig.h"
#include "Input/AsceInputComponent.h"
#include "Camera/AsceCameraComponent.h"
#include "AsceGameplayTags.h"
#include "Components/GameFrameworkComponentManager.h"
#include "PlayerMappableInputConfig.h"
#include "Camera/AsceCameraMode.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "InputMappingContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceHeroComponent)

#if WITH_EDITOR
#include "Misc/UObjectToken.h"
#endif	// WITH_EDITOR

namespace AsceHero
{
	static const float LookYawRate = 300.0f;
	static const float LookPitchRate = 165.0f;
};

const FName UAsceHeroComponent::NAME_BindInputsNow("BindInputsNow");
const FName UAsceHeroComponent::NAME_ActorFeatureName("Hero");

UAsceHeroComponent::UAsceHeroComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilityCameraMode = nullptr;
	bReadyToBindInputs = false;
}

void UAsceHeroComponent::OnRegister()
{
	Super::OnRegister();

	if (!GetPawn<APawn>())
	{
		UE_LOG(LogAsce, Error, TEXT("[UAsceHeroComponent::OnRegister] This component has been added to a blueprint whose base class is not a Pawn. To use this component, it MUST be placed on a Pawn Blueprint."));

#if WITH_EDITOR
		if (GIsEditor)
		{
			static const FText Message = NSLOCTEXT("AsceHeroComponent", "NotOnPawnError", "has been added to a blueprint whose base class is not a Pawn. To use this component, it MUST be placed on a Pawn Blueprint. This will cause a crash if you PIE!");
			static const FName HeroMessageLogName = TEXT("AsceHeroComponent");
			
			FMessageLog(HeroMessageLogName).Error()
				->AddToken(FUObjectToken::Create(this, FText::FromString(GetNameSafe(this))))
				->AddToken(FTextToken::Create(Message));
				
			FMessageLog(HeroMessageLogName).Open();
		}
#endif
	}
	else
	{
		// Register with the init state system early, this will only work if this is a game world
		RegisterInitStateFeature();
	}
}

bool UAsceHeroComponent::CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const
{
	check(Manager);

	APawn* Pawn = GetPawn<APawn>();

	if (!CurrentState.IsValid() && DesiredState == AsceGameplayTags::InitState_Spawned)
	{
		// As long as we have a real pawn, let us transition
		//只要有一个Pawn，就允许转移到InitState_Spawned
		if (Pawn)
		{
			return true;
		}
	}
	else if (CurrentState == AsceGameplayTags::InitState_Spawned && DesiredState == AsceGameplayTags::InitState_DataAvailable)
	{
		// The player state is required.
		//当PlayerState被复制过来后，才可以尝试转移到DataAvailable
		if (!GetPlayerState<AAscePlayerState>())
		{
			return false;
		}
		
		// If we're authority or autonomous, we need to wait for a controller with registered ownership of the player state.
		// 如果是权威端（通常是单机时）或者自主代理，必须等待Controller和PlayerState建立联系
		if (Pawn->GetLocalRole() != ROLE_SimulatedProxy)
		{
			AController* Controller = GetController<AController>();

			const bool bHasControllerPairedWithPS = (Controller != nullptr) && \
				(Controller->PlayerState != nullptr) && \
				(Controller->PlayerState->GetOwner() == Controller);

			if (!bHasControllerPairedWithPS)
			{
				return false;
			}
		}

		const bool bIsLocallyControlled = Pawn->IsLocallyControlled();
		const bool bIsBot = Pawn->IsBotControlled();

		if (bIsLocallyControlled && !bIsBot)
		{
			AAscePlayerController* AscePC = GetController<AAscePlayerController>();

			// The input component and local player is required when locally controlled.
			// 如果是本地控制，必须InputComponent和LocalPlayer合法
			// 如果是模拟代理，则不需要输入组件和LocalPlayer就能转换
			if (!Pawn->InputComponent || !AscePC || !AscePC->GetLocalPlayer())
			{
				return false;
			}
		}

		return true;
	}
	else if (CurrentState == AsceGameplayTags::InitState_DataAvailable && DesiredState == AsceGameplayTags::InitState_DataInitialized)
	{
		//想要转移到DDataInitialized必须保证PlayerState已复制，并且PawnExtensionComponnet到达DataInitialized状态
		// Wait for player state and extension component
		AAscePlayerState* AscePS = GetPlayerState<AAscePlayerState>();

		return AscePS && Manager->HasFeatureReachedInitState(Pawn, UAscePawnExtensionComponent::NAME_ActorFeatureName, AsceGameplayTags::InitState_DataInitialized);
	}
	else if (CurrentState == AsceGameplayTags::InitState_DataInitialized && DesiredState == AsceGameplayTags::InitState_GameplayReady)
	{
		// TODO add ability initialization checks?
		//目前来说，PawnExtensionData到达DataInitialized的前提是所有其他Feature到达DataAviliable，随后这些Feature可以转移到DataInitialized
		//并且转移到GameplayReady只需要DataInitialized即可，PawnExtensionData目前也是到达DataInitialized就可以直接转移到GameplayReady了
		return true;
	}

	return false;
}

void UAsceHeroComponent::HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState)
{
	//尝试状态转移且成功后调用
	if (CurrentState == AsceGameplayTags::InitState_DataAvailable && DesiredState == AsceGameplayTags::InitState_DataInitialized)
	{
		//当尝试从DataAvilable转移到DataInitialiezed且成功时，此时PawnExtComp必然已经是DataInitialized
		//此处初始化ASC, PlayerInput, CameraMode
		APawn* Pawn = GetPawn<APawn>();
		AAscePlayerState* AscePS = GetPlayerState<AAscePlayerState>();
		if (!ensure(Pawn && AscePS))
		{
			return;
		}

		const UAscePawnData* PawnData = nullptr;

		if (UAscePawnExtensionComponent* PawnExtComp = UAscePawnExtensionComponent::FindPawnExtensionComponent(Pawn))
		{
			PawnData = PawnExtComp->GetPawnData<UAscePawnData>();

			// The player state holds the persistent data for this player (state that persists across deaths and multiple pawns).
			// The ability system component and attribute sets live on the player state.
			// ASC在PlayerState上，此时尝试初始化ASC
			PawnExtComp->InitializeAbilitySystem(AscePS->GetAsceAbilitySystemComponent(), AscePS);
		}

		if (AAscePlayerController* AscePC = GetController<AAscePlayerController>())
		{
			if (Pawn->InputComponent != nullptr)
			{
				//初始化玩家输入
				InitializePlayerInput(Pawn->InputComponent);
			}
		}

		// Hook up the delegate for all pawns, in case we spectate later
		if (PawnData)
		{
			if (UAsceCameraComponent* CameraComponent = UAsceCameraComponent::FindCameraComponent(Pawn))
			{
				CameraComponent->DetermineCameraModeDelegate.BindUObject(this, &ThisClass::DetermineCameraMode);
			}
		}
	}
}

void UAsceHeroComponent::OnActorInitStateChanged(const FActorInitStateChangedParams& Params)
{
	if (Params.FeatureName == UAscePawnExtensionComponent::NAME_ActorFeatureName)
	{
		if (Params.FeatureState == AsceGameplayTags::InitState_DataInitialized)
		{
			// If the extension component says all all other components are initialized, try to progress to next state
			CheckDefaultInitialization();
		}
	}
}

void UAsceHeroComponent::CheckDefaultInitialization()
{
	static const TArray<FGameplayTag> StateChain = { AsceGameplayTags::InitState_Spawned, AsceGameplayTags::InitState_DataAvailable, AsceGameplayTags::InitState_DataInitialized, AsceGameplayTags::InitState_GameplayReady };

	// This will try to progress from spawned (which is only set in BeginPlay) through the data initialization stages until it gets to gameplay ready
	ContinueInitStateChain(StateChain);
}

void UAsceHeroComponent::BeginPlay()
{
	Super::BeginPlay();

	// Listen for when the pawn extension component changes init state
	BindOnActorInitStateChanged(UAscePawnExtensionComponent::NAME_ActorFeatureName, FGameplayTag(), false);

	// Notifies that we are done spawning, then try the rest of initialization
	ensure(TryToChangeInitState(AsceGameplayTags::InitState_Spawned));
	CheckDefaultInitialization();
}

void UAsceHeroComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterInitStateFeature();

	Super::EndPlay(EndPlayReason);
}

void UAsceHeroComponent::InitializePlayerInput(UInputComponent* PlayerInputComponent)
{
	check(PlayerInputComponent);

	const APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		return;
	}

	const APlayerController* PC = GetController<APlayerController>();
	check(PC);

	const UAsceLocalPlayer* LP = Cast<UAsceLocalPlayer>(PC->GetLocalPlayer());
	check(LP);

	UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	check(Subsystem);

	Subsystem->ClearAllMappings();

	if (const UAscePawnExtensionComponent* PawnExtComp = UAscePawnExtensionComponent::FindPawnExtensionComponent(Pawn))
	{
		if (const UAscePawnData* PawnData = PawnExtComp->GetPawnData<UAscePawnData>())
		{
			if (const UAsceInputConfig* InputConfig = PawnData->InputConfig)
			{
				
				//先处理默认输入映射
				for (const FInputMappingContextAndPriority& Mapping : DefaultInputMappings)
				{
					if (UInputMappingContext* IMC = Mapping.InputMapping.LoadSynchronous())
					{
						if (Mapping.bRegisterWithSettings)
						{
							if (UEnhancedInputUserSettings* Settings = Subsystem->GetUserSettings())
							{
								Settings->RegisterInputMappingContext(IMC);
							}
							
							FModifyContextOptions Options = {};
							Options.bIgnoreAllPressedKeysUntilRelease = false;
							// Actually add the config to the local player	
							//添加输入映射上下文
							Subsystem->AddMappingContext(IMC, Mapping.Priority, Options);
						}
					}
				}

				// The Lyra Input Component has some additional functions to map Gameplay Tags to an Input Action.
				// If you want this functionality but still want to change your input component class, make it a subclass
				// of the ULyraInputComponent or modify this component accordingly.
				
				//一些扩展用法，输入组件有一些额外的函数来映射GameplayTags到一个InputAction，如果需要这个功能但是仍然想要改变输入组件的类。
				//就继承UAsceInputComponent或者直接修改这个组件。
				
				UAsceInputComponent* AsceIC = Cast<UAsceInputComponent>(PlayerInputComponent);
				if (ensureMsgf(AsceIC, TEXT("Unexpected Input Component class! The Gameplay Abilities will not be bound to their inputs. Change the input component to ULyraInputComponent or a subclass of it.")))
				{
					// Add the key mappings that may have been set by the player
					// 添加按键映射(实际上什么也不做)
					AsceIC->AddInputMappings(InputConfig, Subsystem);

					// This is where we actually bind and input action to a gameplay tag, which means that Gameplay Ability Blueprints will
					// be triggered directly by these input actions Triggered events.
					// 此处将InputAction和GameplayTag进行绑定，这意味着GA将会直接被这些按键输入给触发
					TArray<uint32> BindHandles;
					
					// 将InputConfig中的按键和对应的GameplayTag进行绑定
					AsceIC->BindAbilityActions(InputConfig, this, &ThisClass::Input_AbilityInputTagPressed, &ThisClass::Input_AbilityInputTagReleased, /*out*/ BindHandles);

					
					// 绑定NativeAction，比如走路，转视角这些不会导致能力触发的输入
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Move, ETriggerEvent::Triggered, this, &ThisClass::Input_Move, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Look_Mouse, ETriggerEvent::Triggered, this, &ThisClass::Input_LookMouse, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Look_Stick, ETriggerEvent::Triggered, this, &ThisClass::Input_LookStick, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Crouch, ETriggerEvent::Triggered, this, &ThisClass::Input_Crouch, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_AutoRun, ETriggerEvent::Triggered, this, &ThisClass::Input_AutoRun, /*bLogIfNotFound=*/ false);
				}
			}
		}
	}

	if (ensure(!bReadyToBindInputs))
	{
		bReadyToBindInputs = true;
	}
 
	//触发所有Controller和Pawn的ReciverClass事件
	UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(const_cast<APlayerController*>(PC), NAME_BindInputsNow);
	UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(const_cast<APawn*>(Pawn), NAME_BindInputsNow);
}

void UAsceHeroComponent::AddAdditionalInputConfig(const UAsceInputConfig* InputConfig)
{
	//添加额外的输入配置(仅仅会添加其中的AbilityInputActions部分，Native部分将不会被忽略)
	TArray<uint32> BindHandles;

	const APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		return;
	}
	
	const APlayerController* PC = GetController<APlayerController>();
	check(PC);

	const ULocalPlayer* LP = PC->GetLocalPlayer();
	check(LP);

	UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	check(Subsystem);

	if (const UAscePawnExtensionComponent* PawnExtComp = UAscePawnExtensionComponent::FindPawnExtensionComponent(Pawn))
	{
		UAsceInputComponent* AsceIC = Pawn->FindComponentByClass<UAsceInputComponent>();
		if (ensureMsgf(AsceIC, TEXT("Unexpected Input Component class! The Gameplay Abilities will not be bound to their inputs. Change the input component to UAsceInputComponent or a subclass of it.")))
		{
			AsceIC->BindAbilityActions(InputConfig, this, &ThisClass::Input_AbilityInputTagPressed, &ThisClass::Input_AbilityInputTagReleased, /*out*/ BindHandles);
		}
	}
}

void UAsceHeroComponent::RemoveAdditionalInputConfig(const UAsceInputConfig* InputConfig)
{
	//@TODO: Implement me!
}

bool UAsceHeroComponent::IsReadyToBindInputs() const
{
	return bReadyToBindInputs;
}

void UAsceHeroComponent::Input_AbilityInputTagPressed(FGameplayTag InputTag)
{
	//按键触发时，将对应的GameplayTag发送到ASC，此时ASC决定如何处理能力
	if (const APawn* Pawn = GetPawn<APawn>())
	{
		if (const UAscePawnExtensionComponent* PawnExtComp = UAscePawnExtensionComponent::FindPawnExtensionComponent(Pawn))
		{
			if (UAsceAbilitySystemComponent* AsceASC = PawnExtComp->GetAsceAbilitySystemComponent())
			{
				AsceASC->AbilityInputTagPressed(InputTag);
			}
		}	
	}
}

void UAsceHeroComponent::Input_AbilityInputTagReleased(FGameplayTag InputTag)
{
	const APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		return;
	}

	if (const UAscePawnExtensionComponent* PawnExtComp = UAscePawnExtensionComponent::FindPawnExtensionComponent(Pawn))
	{
		if (UAsceAbilitySystemComponent* AsceASC = PawnExtComp->GetAsceAbilitySystemComponent())
		{
			AsceASC->AbilityInputTagReleased(InputTag);
		}
	}
}

void UAsceHeroComponent::Input_Move(const FInputActionValue& InputActionValue)
{
	//移动输入，作为NativeInput，直接处理
	APawn* Pawn = GetPawn<APawn>();
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;

	// If the player has attempted to move again then cancel auto running
	// 如果是玩家尝试再次移动，取消自动奔跑
	if (AAscePlayerController* AsceController = Cast<AAscePlayerController>(Controller))
	{
		AsceController->SetIsAutoRunning(false);
	}
	
	if (Controller)
	{
		const FVector2D Value = InputActionValue.Get<FVector2D>();
		const FRotator MovementRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);

		if (Value.X != 0.0f)
		{
			const FVector MovementDirection = MovementRotation.RotateVector(FVector::RightVector);
			Pawn->AddMovementInput(MovementDirection, Value.X);
		}

		if (Value.Y != 0.0f)
		{
			const FVector MovementDirection = MovementRotation.RotateVector(FVector::ForwardVector);
			Pawn->AddMovementInput(MovementDirection, Value.Y);
		}
	}
}

void UAsceHeroComponent::Input_LookMouse(const FInputActionValue& InputActionValue)
{
	APawn* Pawn = GetPawn<APawn>();

	if (!Pawn)
	{
		return;
	}
	
	const FVector2D Value = InputActionValue.Get<FVector2D>();

	if (Value.X != 0.0f)
	{
		Pawn->AddControllerYawInput(Value.X);
	}

	if (Value.Y != 0.0f)
	{
		Pawn->AddControllerPitchInput(Value.Y);
	}
}

void UAsceHeroComponent::Input_LookStick(const FInputActionValue& InputActionValue)
{
	APawn* Pawn = GetPawn<APawn>();

	if (!Pawn)
	{
		return;
	}
	
	const FVector2D Value = InputActionValue.Get<FVector2D>();

	const UWorld* World = GetWorld();
	check(World);

	if (Value.X != 0.0f)
	{
		Pawn->AddControllerYawInput(Value.X * AsceHero::LookYawRate * World->GetDeltaSeconds());
	}

	if (Value.Y != 0.0f)
	{
		Pawn->AddControllerPitchInput(Value.Y * AsceHero::LookPitchRate * World->GetDeltaSeconds());
	}
}

void UAsceHeroComponent::Input_Crouch(const FInputActionValue& InputActionValue)
{
	if (AAsceCharacter* Character = GetPawn<AAsceCharacter>())
	{
		Character->ToggleCrouch();
	}
}

void UAsceHeroComponent::Input_AutoRun(const FInputActionValue& InputActionValue)
{
	if (APawn* Pawn = GetPawn<APawn>())
	{
		if (AAscePlayerController* Controller = Cast<AAscePlayerController>(Pawn->GetController()))
		{
			// Toggle auto running
			Controller->SetIsAutoRunning(!Controller->GetIsAutoRunning());
		}	
	}
}

TSubclassOf<UAsceCameraMode> UAsceHeroComponent::DetermineCameraMode() const
{
	if (AbilityCameraMode)
	{
		return AbilityCameraMode;
	}

	const APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		return nullptr;
	}

	if (UAscePawnExtensionComponent* PawnExtComp = UAscePawnExtensionComponent::FindPawnExtensionComponent(Pawn))
	{
		if (const UAscePawnData* PawnData = PawnExtComp->GetPawnData<UAscePawnData>())
		{
			return PawnData->DefaultCameraMode;
		}
	}

	return nullptr;
}

void UAsceHeroComponent::SetAbilityCameraMode(TSubclassOf<UAsceCameraMode> CameraMode, const FGameplayAbilitySpecHandle& OwningSpecHandle)
{
	if (CameraMode)
	{
		AbilityCameraMode = CameraMode;
		AbilityCameraModeOwningSpecHandle = OwningSpecHandle;
	}
}

void UAsceHeroComponent::ClearAbilityCameraMode(const FGameplayAbilitySpecHandle& OwningSpecHandle)
{
	if (AbilityCameraModeOwningSpecHandle == OwningSpecHandle)
	{
		AbilityCameraMode = nullptr;
		AbilityCameraModeOwningSpecHandle = FGameplayAbilitySpecHandle();
	}
}

