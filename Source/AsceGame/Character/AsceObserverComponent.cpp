// Fill out your copyright notice in the Description page of Project Settings.


#include "AsceObserverComponent.h"

#include "AsceGameplayTags.h"
#include "AsceLogChannels.h"
#include "AscePawnData.h"
#include "AscePawnExtensionComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/AsceCameraComponent.h"
#include "Camera/AsceCameraMode.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/AsceInputComponent.h"
#include "InputMappingContext.h"
#include "Misc/UObjectToken.h"
#include "Player/AsceLocalPlayer.h"
#include "Player/AscePlayerController.h"
#include "Player/AscePlayerState.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "Widgets/SWidget.h"
#include "GameFeatures/GameFeatureAction_AddInputContextMapping.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceObserverComponent)
const FName UAsceObserverComponent::NAME_BindInputsNow("ObserverBindInputsNow");
const FName UAsceObserverComponent::NAME_ActorFeatureName("Observer");

namespace
{
	/** 指针是否落在 UI 上（不是 3D 视口）；等价于旧版 `APlayerController::IsPointerOverGameObject`。 */
	bool IsPointerOverUI()
	{
		if (!FSlateApplication::IsInitialized())
		{
			return false;
		}
		FSlateApplication& SlateApp = FSlateApplication::Get();
		const FVector2D CursorPos = SlateApp.GetCursorPos();
		const FWidgetPath WidgetPath = SlateApp.LocateWindowUnderMouse(CursorPos, SlateApp.GetInteractiveTopLevelWindows());
		if (!WidgetPath.IsValid())
		{
			return false;
		}
		// 命中叶子是 SViewport ⇒ 指针在世界里；命中叶子是 UMG/Slate 控件 ⇒ 在 UI 上
		const TSharedRef<SWidget>& Leaf = WidgetPath.GetLastWidget();
		return !Leaf->GetType().IsEqual(TEXT("SViewport"));
	}
}

UAsceObserverComponent::UAsceObserverComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	AbilityCameraMode = nullptr;
	bReadyToBindInputs = false;
}

void UAsceObserverComponent::SetActorLocation(FVector InLocation)
{
	CachedLocationToUpdate = InLocation;
}

FVector UAsceObserverComponent::GetActorLocation()
{
	return CachedLocationToUpdate;
}

void UAsceObserverComponent::SetAbilityCameraMode(TSubclassOf<UAsceCameraMode> CameraMode,
                                                  const FGameplayAbilitySpecHandle& OwningSpecHandle)
{
	if (CameraMode)
	{
		AbilityCameraMode = CameraMode;
		AbilityCameraModeOwningSpecHandle = OwningSpecHandle;
	}
}

void UAsceObserverComponent::ClearAbilityCameraMode(const FGameplayAbilitySpecHandle& OwningSpecHandle)
{
	if (AbilityCameraModeOwningSpecHandle == OwningSpecHandle)
	{
		AbilityCameraMode = nullptr;
		AbilityCameraModeOwningSpecHandle = FGameplayAbilitySpecHandle();
	}
}

void UAsceObserverComponent::AddAdditionalInputConfig(const UAsceInputConfig* InputConfig)
{
	//添加额外的输入配置(仅仅会添加其中的AbilityInputActions部分，Native部分将会被忽略)
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

void UAsceObserverComponent::RemoveAdditionalInputConfig(const UAsceInputConfig* InputConfig)
{
}

bool UAsceObserverComponent::IsReadyToBindInputs() const
{
	return bReadyToBindInputs;
}

bool UAsceObserverComponent::CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState,
	FGameplayTag DesiredState) const
{
	check(Manager);

	APawn* Pawn = GetPawn<APawn>();

	if (!CurrentState.IsValid() && DesiredState == AsceGameplayTags::InitState_Spawned)
	{
		//只要有一个Pawn，就允许转移到InitState_Spawned
		if (Pawn)
		{
			return true;
		}
	}
	else if (CurrentState == AsceGameplayTags::InitState_Spawned && DesiredState == AsceGameplayTags::InitState_DataAvailable)
	{
		//当PlayerState被复制过来后，才可以尝试转移到DataAvailable
		if (!GetPlayerState<AAscePlayerState>())
		{
			return false;
		}
		
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
		return true;
	}

	return false;
}

void UAsceObserverComponent::HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState,
	FGameplayTag DesiredState)
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

void UAsceObserverComponent::OnActorInitStateChanged(const FActorInitStateChangedParams& Params)
{
	if (Params.FeatureName == UAscePawnExtensionComponent::NAME_ActorFeatureName)
	{
		if (Params.FeatureState == AsceGameplayTags::InitState_DataInitialized)
		{
			CheckDefaultInitialization();
		}
	}
}

void UAsceObserverComponent::CheckDefaultInitialization()
{
	static const TArray<FGameplayTag> StateChain = { AsceGameplayTags::InitState_Spawned, AsceGameplayTags::InitState_DataAvailable, AsceGameplayTags::InitState_DataInitialized, AsceGameplayTags::InitState_GameplayReady };

	// This will try to progress from spawned (which is only set in BeginPlay) through the data initialization stages until it gets to gameplay ready
	ContinueInitStateChain(StateChain);
}

void UAsceObserverComponent::OnRegister()
{
	Super::OnRegister();

	if (!GetPawn<APawn>())
	{
		UE_LOG(LogAsce, Error, TEXT("[AsceObserverComponent::OnRegister] This component has been added to a blueprint whose base class is not a Pawn. To use this component, it MUST be placed on a Pawn Blueprint."));

#if WITH_EDITOR
		if (GIsEditor)
		{
			static const FText Message = NSLOCTEXT("AsceObserverComponent", "NotOnPawnError", "has been added to a blueprint whose base class is not a Pawn. To use this component, it MUST be placed on a Pawn Blueprint. This will cause a crash if you PIE!");
			static const FName ObserverMessageLogName = TEXT("AsceObserverComponent");
			
			FMessageLog(ObserverMessageLogName).Error()
				->AddToken(FUObjectToken::Create(this, FText::FromString(GetNameSafe(this))))
				->AddToken(FTextToken::Create(Message));
				
			FMessageLog(ObserverMessageLogName).Open();
		}
#endif
	}
	else
	{
		// Register with the init state system early, this will only work if this is a game world
		RegisterInitStateFeature();
	}
}

void UAsceObserverComponent::BeginPlay()
{
	Super::BeginPlay();

	// Listen for when the pawn extension component changes init state
	BindOnActorInitStateChanged(UAscePawnExtensionComponent::NAME_ActorFeatureName, FGameplayTag(), false);

	// Notifies that we are done spawning, then try the rest of initialization
	ensure(TryToChangeInitState(AsceGameplayTags::InitState_Spawned));
	CheckDefaultInitialization();
	if (IsValid(GetOwner()))
	{
		CachedLocationToUpdate = GetOwner()->GetActorLocation();
	}
}

void UAsceObserverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterInitStateFeature();
	Super::EndPlay(EndPlayReason);
}

void UAsceObserverComponent::TickComponent(float DeltaTime, enum ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	APawn* Pawn = GetOwner<APawn>();
	check(Pawn);
	FVector Delta = Pawn->GetActorLocation() - GetActorLocation();
	if (!Delta.IsNearlyZero())
	{
		Pawn->SetActorLocation(GetActorLocation());
	}
	
	AController* Controller = Pawn->GetController();
	Controller->SetControlRotation(Pawn->GetActorRotation());
}

void UAsceObserverComponent::InitializePlayerInput(UInputComponent* PlayerInputComponent)
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

					
					// 绑定NativeAction
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Perspective_Move, ETriggerEvent::Triggered, this, &ThisClass::Input_MovePerspective, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Shift, ETriggerEvent::Started, this, &ThisClass::Input_Shifting_Start, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Shift, ETriggerEvent::Completed, this, &ThisClass::Input_Shifting_Stop, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Shift, ETriggerEvent::Canceled, this, &ThisClass::Input_Shifting_Stop, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Alternate, ETriggerEvent::Started, this, &ThisClass::Input_Alternate_Start, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Alternate, ETriggerEvent::Completed, this, &ThisClass::Input_Alternate_Stop, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Alternate, ETriggerEvent::Canceled, this, &ThisClass::Input_Alternate_Stop, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Control, ETriggerEvent::Started, this, &ThisClass::Input_Control_Start, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Control, ETriggerEvent::Completed, this, &ThisClass::Input_Control_Stop, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Control, ETriggerEvent::Canceled, this, &ThisClass::Input_Control_Stop, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Select, ETriggerEvent::Started, this, &ThisClass::Input_Select_Start, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Select, ETriggerEvent::Completed, this, &ThisClass::Input_Select_Stop, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Select, ETriggerEvent::Canceled, this, &ThisClass::Input_Select_Cancel, /*bLogIfNotFound=*/ false);
					AsceIC->BindNativeAction(InputConfig, AsceGameplayTags::InputTag_Order_Move, ETriggerEvent::Triggered, this, &ThisClass::Input_Order_Move, /*bLogIfNotFound=*/ false);
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

void UAsceObserverComponent::Input_AbilityInputTagPressed(FGameplayTag InputTag)
{
}

void UAsceObserverComponent::Input_AbilityInputTagReleased(FGameplayTag InputTag)
{
}

void UAsceObserverComponent::Input_MovePerspective(const FInputActionValue& InputActionValue)
{
	//将鼠标移动转化为移动输入，作为NativeInput，直接处理
	APawn* Pawn = GetPawn<APawn>();
	
	if (Pawn)
	{
		FVector RightVector = Pawn->GetActorRightVector();
		FVector UpVector = FVector::CrossProduct(RightVector, FVector::UpVector);
		
		const FVector2D Value = InputActionValue.Get<FVector2D>();

		FVector Location = Pawn->GetActorLocation();
		
		if (Value.X != 0.0f)
		{
			Location -= Value.X * RightVector * MoveSpeed;
		}

		if (Value.Y != 0.0f)
		{
			Location -= Value.Y * UpVector * MoveSpeed;
		}
		
		SetActorLocation(Location);
	}
}

void UAsceObserverComponent::Input_Shifting_Start(const FInputActionValue& InputActionValue)
{
	bIsShifting = true;
}

void UAsceObserverComponent::Input_Shifting_Stop(const FInputActionValue& InputActionValue)
{
	bIsShifting = false;
}

void UAsceObserverComponent::Input_Alternate_Start(const FInputActionValue& InputActionValue)
{
	bIsAlternating = true;
}

void UAsceObserverComponent::Input_Alternate_Stop(const FInputActionValue& InputActionValue)
{
	bIsAlternating = false;
}

void UAsceObserverComponent::Input_Control_Start(const FInputActionValue& InputActionValue)
{
	bIsControlling = true;
}

void UAsceObserverComponent::Input_Control_Stop(const FInputActionValue& InputActionValue)
{
	bIsControlling = false;
}

void UAsceObserverComponent::Input_Select_Start(const FInputActionValue& InputActionValue)
{
	if (APawn* Pawn = GetPawn<APawn>())
	{
		APlayerController* PC = Pawn->GetController<APlayerController>();
		if (!IsValid(PC))
		{
			return;
		}
		// 指针在 UI（检视器/按钮等）上：不开始框选，避免选中框穿透到面板里
		// （面板可见性为可命中时 Slate 命中叶子不是 SViewport）
		if (IsPointerOverUI())
		{
			return;
		}
		PC->GetMousePosition(SelectionStartPosition.X, SelectionStartPosition.Y);
		bCachedIsShifting = bIsShifting;
		bCachedIsAlternating = bIsAlternating;
		bCachedIsControlling = bIsControlling;
		bIsSelecting = true;
	}
}

void UAsceObserverComponent::Input_Select_Stop(const FInputActionValue& InputActionValue)
{
	if (bIsSelecting)
	{
		if (APawn* Pawn = GetPawn<APawn>())
		{
			APlayerController* PC = Pawn->GetController<APlayerController>();
			if (!IsValid(PC))
			{
				return;
			}
			PC->GetMousePosition(SelectionEndPosition.X, SelectionEndPosition.Y);
			
			//暂时不提供多个组合键，比如shift+alt+左键这种，目前只考虑两个按键的组合
			if (FVector2D::Distance(SelectionStartPosition, SelectionEndPosition) < DragSelectPixelThreshold)
			{
				//小于框选的最小阈值，我们认为这是一次点击
				if (bCachedIsShifting)
				{
					//Shift点击作为最高优先级
					OnAdditiveSelect.Broadcast(SelectionEndPosition);
				}
				else if (bCachedIsAlternating)
				{
					//随后是Alt点击
					OnAlternateSelect.Broadcast(SelectionEndPosition);
				}
				else if (bCachedIsControlling)
				{
					//Ctrl点击优先级最低，我甚至还没想好该用来干什么
					OnControlSelect.Broadcast(SelectionEndPosition);
				}
				else
				{
					OnSelect.Broadcast(SelectionEndPosition);
				}
			}
			else
			{
				//这是一次框选
				if (bCachedIsShifting)
				{
					OnAdditiveDragSelect.Broadcast(SelectionStartPosition, SelectionEndPosition);
				}
				else
				{
					OnDragSelect.Broadcast(SelectionStartPosition, SelectionEndPosition);
				}
			}
		}
		bIsSelecting = false;
		bCachedIsShifting = false;
		bCachedIsAlternating = false;
		bCachedIsControlling = false;
	}
}

void UAsceObserverComponent::Input_Select_Cancel(const FInputActionValue& InputActionValue)
{
	bIsSelecting = false;
	bCachedIsShifting = false;
	bCachedIsAlternating = false;
	bCachedIsControlling = false;
}

void UAsceObserverComponent::Input_Order_Move(const FInputActionValue& InputActionValue)
{
	if (APawn* Pawn = GetPawn<APawn>())
	{
		APlayerController* PC = Pawn->GetController<APlayerController>();
		if (!IsValid(PC))
		{
			return;
		}
		// 指针在 UI 上：不下发移动指令
		if (IsPointerOverUI())
		{
			return;
		}
		FVector2D MousePosition;
		PC->GetMousePosition(MousePosition.X, MousePosition.Y);
		OnMoveOrder.Broadcast(MousePosition);
	}
}


TSubclassOf<UAsceCameraMode> UAsceObserverComponent::DetermineCameraMode() const
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


