// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsceCameraMode.h"

#include "Components/CapsuleComponent.h"
#include "Engine/Canvas.h"
#include "GameFramework/Character.h"
#include "AsceCameraComponent.h"
#include "AscePlayerCameraManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceCameraMode)


//////////////////////////////////////////////////////////////////////////
// FLyraCameraModeView
//////////////////////////////////////////////////////////////////////////
FAsceCameraModeView::FAsceCameraModeView()
	: Location(ForceInit)
	, Rotation(ForceInit)
	, ControlRotation(ForceInit)
	, FieldOfView(ASCE_CAMERA_DEFAULT_FOV)
{
}

void FAsceCameraModeView::Blend(const FAsceCameraModeView& Other, float OtherWeight)
{
	if (OtherWeight <= 0.0f)
	{
		return;
	}
	else if (OtherWeight >= 1.0f)
	{
		*this = Other;
		return;
	}

	Location = FMath::Lerp(Location, Other.Location, OtherWeight);

	const FRotator DeltaRotation = (Other.Rotation - Rotation).GetNormalized();
	Rotation = Rotation + (OtherWeight * DeltaRotation);

	const FRotator DeltaControlRotation = (Other.ControlRotation - ControlRotation).GetNormalized();
	ControlRotation = ControlRotation + (OtherWeight * DeltaControlRotation);

	FieldOfView = FMath::Lerp(FieldOfView, Other.FieldOfView, OtherWeight);
}


//////////////////////////////////////////////////////////////////////////
// UAsceCameraMode
//////////////////////////////////////////////////////////////////////////
UAsceCameraMode::UAsceCameraMode()
{
	FieldOfView = ASCE_CAMERA_DEFAULT_FOV;
	ViewPitchMin = ASCE_CAMERA_DEFAULT_PITCH_MIN;
	ViewPitchMax = ASCE_CAMERA_DEFAULT_PITCH_MAX;

	BlendTime = 0.5f;
	BlendFunction = EAsceCameraModeBlendFunction::EaseOut;
	BlendExponent = 4.0f;
	BlendAlpha = 1.0f;
	BlendWeight = 1.0f;
}

UAsceCameraComponent* UAsceCameraMode::GetAsceCameraComponent() const
{
	return CastChecked<UAsceCameraComponent>(GetOuter());
}

UWorld* UAsceCameraMode::GetWorld() const
{
	return HasAnyFlags(RF_ClassDefaultObject) ? nullptr : GetOuter()->GetWorld();
}

AActor* UAsceCameraMode::GetTargetActor() const
{
	const UAsceCameraComponent* AsceCameraComponent = GetAsceCameraComponent();

	return AsceCameraComponent->GetTargetActor();
}

FVector UAsceCameraMode::GetPivotLocation() const
{
	const AActor* TargetActor = GetTargetActor();
	check(TargetActor);

	if (const APawn* TargetPawn = Cast<APawn>(TargetActor))
	{
		// Height adjustments for characters to account for crouching.
		if (const ACharacter* TargetCharacter = Cast<ACharacter>(TargetPawn))
		{
			const ACharacter* TargetCharacterCDO = TargetCharacter->GetClass()->GetDefaultObject<ACharacter>();
			check(TargetCharacterCDO);

			const UCapsuleComponent* CapsuleComp = TargetCharacter->GetCapsuleComponent();
			check(CapsuleComp);

			const UCapsuleComponent* CapsuleCompCDO = TargetCharacterCDO->GetCapsuleComponent();
			check(CapsuleCompCDO);

			const float DefaultHalfHeight = CapsuleCompCDO->GetUnscaledCapsuleHalfHeight();
			const float ActualHalfHeight = CapsuleComp->GetUnscaledCapsuleHalfHeight();
			const float HeightAdjustment = (DefaultHalfHeight - ActualHalfHeight) + TargetCharacterCDO->BaseEyeHeight;

			return TargetCharacter->GetActorLocation() + (FVector::UpVector * HeightAdjustment);
		}

		return TargetPawn->GetPawnViewLocation();
	}

	return TargetActor->GetActorLocation();
}

FRotator UAsceCameraMode::GetPivotRotation() const
{
	const AActor* TargetActor = GetTargetActor();
	check(TargetActor);

	//对于Pawn来说获取ControllerRotation
	//对于Actor来讲，直接获取ActorRotation
	if (const APawn* TargetPawn = Cast<APawn>(TargetActor))
	{
		return TargetPawn->GetViewRotation();
	}

	return TargetActor->GetActorRotation();
}

void UAsceCameraMode::UpdateCameraMode(float DeltaTime)
{
	//每帧更新相机模式
	UpdateView(DeltaTime);
	UpdateBlending(DeltaTime);
}

void UAsceCameraMode::UpdateView(float DeltaTime)
{
	FVector PivotLocation = GetPivotLocation();
	FRotator PivotRotation = GetPivotRotation();

	PivotRotation.Pitch = FMath::ClampAngle(PivotRotation.Pitch, ViewPitchMin, ViewPitchMax);

	View.Location = PivotLocation;
	View.Rotation = PivotRotation;
	View.ControlRotation = View.Rotation;
	View.FieldOfView = FieldOfView;
}

void UAsceCameraMode::SetBlendWeight(float Weight)
{
	BlendWeight = FMath::Clamp(Weight, 0.0f, 1.0f);

	// Since we're setting the blend weight directly, we need to calculate the blend alpha to account for the blend function.
	const float InvExponent = (BlendExponent > 0.0f) ? (1.0f / BlendExponent) : 1.0f;

	//计算混合Alpha值，Alpha值代表x轴，即目前的混合百分比进度，为 当前混合时间/总的混合时间
	//Weight代表y轴，即目前混合了百分之多少到目标值  假设目标的相机角度为90度，原相机的角度为30度，那么Weight为0.5时，当前相机为60度
	//此时的Alpha可能为0.1，即时间刚过去十分之一，混合进度就到了0.5，这意味着后面的混合曲线斜率会下降，因为Weight到达1时Alpha也到达1
	//对于线性混合，weight == alpha
	//通常来说，对于EaseIn函数的计算方式为： Alpha^Exponent = Weight
	//反向可得 Weight^(1/Exponnet) = Alpha
	//其他两种类型的混合函数实际上都是EaseIn的变体，因此传入InvExponent和BlendWeight就能计算出Alpha（y = x^n 函数的性质）
	
	switch (BlendFunction)
	{
	case EAsceCameraModeBlendFunction::Linear:
		BlendAlpha = BlendWeight;
		break;

	case EAsceCameraModeBlendFunction::EaseIn:
		BlendAlpha = FMath::InterpEaseIn(0.0f, 1.0f, BlendWeight, InvExponent);
		break;

	case EAsceCameraModeBlendFunction::EaseOut:
		BlendAlpha = FMath::InterpEaseOut(0.0f, 1.0f, BlendWeight, InvExponent);
		break;

	case EAsceCameraModeBlendFunction::EaseInOut:
		BlendAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, BlendWeight, InvExponent);
		break;

	default:
		checkf(false, TEXT("SetBlendWeight: Invalid BlendFunction [%d]\n"), (uint8)BlendFunction);
		break;
	}
}

void UAsceCameraMode::UpdateBlending(float DeltaTime)
{
	//BlendAlpha就是混合时间的百分比
	//此处通过BlendTime计算出BlendApha进而计算出BlendWeight
	//BlendWeight直接作用于初始数据得到当前的混合数据
	
	if (BlendTime > 0.0f)
	{
		BlendAlpha += (DeltaTime / BlendTime);
		BlendAlpha = FMath::Min(BlendAlpha, 1.0f);
	}
	else
	{
		BlendAlpha = 1.0f;
	}

	const float Exponent = (BlendExponent > 0.0f) ? BlendExponent : 1.0f;

	switch (BlendFunction)
	{
	case EAsceCameraModeBlendFunction::Linear:
		BlendWeight = BlendAlpha;
		break;

	case EAsceCameraModeBlendFunction::EaseIn:
		BlendWeight = FMath::InterpEaseIn(0.0f, 1.0f, BlendAlpha, Exponent);
		break;

	case EAsceCameraModeBlendFunction::EaseOut:
		BlendWeight = FMath::InterpEaseOut(0.0f, 1.0f, BlendAlpha, Exponent);
		break;

	case EAsceCameraModeBlendFunction::EaseInOut:
		BlendWeight = FMath::InterpEaseInOut(0.0f, 1.0f, BlendAlpha, Exponent);
		break;

	default:
		checkf(false, TEXT("UpdateBlending: Invalid BlendFunction [%d]\n"), (uint8)BlendFunction);
		break;
	}
}

void UAsceCameraMode::DrawDebug(UCanvas* Canvas) const
{
	check(Canvas);

	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;

	DisplayDebugManager.SetDrawColor(FColor::White);
	DisplayDebugManager.DrawString(FString::Printf(TEXT("      AsceCameraMode: %s (%f)"), *GetName(), BlendWeight));
}


//////////////////////////////////////////////////////////////////////////
// UAsceCameraModeStack
//////////////////////////////////////////////////////////////////////////
UAsceCameraModeStack::UAsceCameraModeStack()
{
	bIsActive = true;
}

void UAsceCameraModeStack::ActivateStack()
{
	if (!bIsActive)
	{
		bIsActive = true;

		// Notify camera modes that they are being activated.
		// 激活栈中的相机模式
		for (UAsceCameraMode* CameraMode : CameraModeStack)
		{
			check(CameraMode);
			CameraMode->OnActivation();
		}
	}
}

void UAsceCameraModeStack::DeactivateStack()
{
	if (bIsActive)
	{
		bIsActive = false;

		// Notify camera modes that they are being deactivated.
		// 取消激活相机模式
		for (UAsceCameraMode* CameraMode : CameraModeStack)
		{
			check(CameraMode);
			CameraMode->OnDeactivation();
		}
	}
}

void UAsceCameraModeStack::PushCameraMode(TSubclassOf<UAsceCameraMode> CameraModeClass)
{
	//相机模式压栈
	if (!CameraModeClass)
	{
		return;
	}

	UAsceCameraMode* CameraMode = GetCameraModeInstance(CameraModeClass);
	check(CameraMode);

	int32 StackSize = CameraModeStack.Num();

	//如果这个相机模式已经在栈顶了，直接返回
	if ((StackSize > 0) && (CameraModeStack[0] == CameraMode))
	{
		// Already top of stack.
		return;
	}

	// See if it's already in the stack and remove it.
	// Figure out how much it was contributing to the stack.
	// 检查这个相机模式是否已经在栈中，然后移除它，并且计算其贡献值
	// 贡献值计算方式如下：
	// 从栈顶开始检查，当前的相机模式为要压入的相机模式时，Contribution = Contribution * 当前位置相机模式的混合权重， break
	// 当前相机模式不为要压入的相机模式时，Contribution = Contribution * (1 - 当前位置的相机模式的混合权重)
	// 即与自己的相机模式混合权重成正比，与其他相机的混合权重成反比
	// 如果没有找到已有的相机模式，则Contribution = 0
	int32 ExistingStackIndex = INDEX_NONE;
	float ExistingStackContribution = 1.0f;

	for (int32 StackIndex = 0; StackIndex < StackSize; ++StackIndex)
	{
		if (CameraModeStack[StackIndex] == CameraMode)
		{
			ExistingStackIndex = StackIndex;
			ExistingStackContribution *= CameraMode->GetBlendWeight();
			break;
		}
		else
		{
			ExistingStackContribution *= (1.0f - CameraModeStack[StackIndex]->GetBlendWeight());
		}
	}

	if (ExistingStackIndex != INDEX_NONE)
	{
		CameraModeStack.RemoveAt(ExistingStackIndex);
		StackSize--;
	}
	else
	{
		ExistingStackContribution = 0.0f;
	}

	//使用Contribution来作为初始的混合权重（如果混合时间为0就直接设置混合权重为1，或者这是入栈的第一个相机模式，也直接设置混合权重为1）
	// Decide what initial weight to start with.
	const bool bShouldBlend = ((CameraMode->GetBlendTime() > 0.0f) && (StackSize > 0));
	const float BlendWeight = (bShouldBlend ? ExistingStackContribution : 1.0f);

	CameraMode->SetBlendWeight(BlendWeight);

	// Add new entry to top of stack.
	CameraModeStack.Insert(CameraMode, 0);

	// Make sure stack bottom is always weighted 100%.
	// 栈底的是稳定相机模式，其混合权重总是为1，这里是为了处理当前push进来的相机模式正好就是之前的稳定相机模式的情况
	// 其从栈底移动到栈顶之后，新的栈底相机模式可能还没有来的及混合到权重为1的状态，因此直接设置其权重为1
	CameraModeStack.Last()->SetBlendWeight(1.0f);

	// Let the camera mode know if it's being added to the stack.
	// 要是之前没有在相机栈中，就先激活一下
	if (ExistingStackIndex == INDEX_NONE)
	{
		CameraMode->OnActivation();
	}
}

bool UAsceCameraModeStack::EvaluateStack(float DeltaTime, FAsceCameraModeView& OutCameraModeView)
{
	//每帧更新相机栈
	if (!bIsActive)
	{
		return false;
	}

	UpdateStack(DeltaTime);
	BlendStack(OutCameraModeView);

	return true;
}

UAsceCameraMode* UAsceCameraModeStack::GetCameraModeInstance(TSubclassOf<UAsceCameraMode> CameraModeClass)
{
	check(CameraModeClass);

	// First see if we already created one.
	// 先遍历一下是不是已经在池子里面了
	for (UAsceCameraMode* CameraMode : CameraModeInstances)
	{
		if ((CameraMode != nullptr) && (CameraMode->GetClass() == CameraModeClass))
		{
			return CameraMode;
		}
	}

	// Not found, so we need to create it.
	// 池子里面没有找到就创建一个新的
	/*TODO 这里是用的类，然后创建Object，不用CDO是因为其中有很多成员变量在运行中会修改值，是不是可以考虑多个相同类的相机模式但是参数不同的做法？
	**但是必要性并不大，大不了就算改一点参数也重新创建一个类就行了，这里本来就是希望使用类来进行编辑参数而不是实例来编辑参数
	*/
	UAsceCameraMode* NewCameraMode = NewObject<UAsceCameraMode>(GetOuter(), CameraModeClass, NAME_None, RF_NoFlags);
	check(NewCameraMode);

	CameraModeInstances.Add(NewCameraMode);

	return NewCameraMode;
}

void UAsceCameraModeStack::UpdateStack(float DeltaTime)
{
	const int32 StackSize = CameraModeStack.Num();
	if (StackSize <= 0)
	{
		return;
	}

	int32 RemoveCount = 0;
	int32 RemoveIndex = INDEX_NONE;

	//每次更新时，找到最靠上的混合权重为1的相机模式，其变为顶层稳定相机，此时弹出其下面的所有相机模式
	for (int32 StackIndex = 0; StackIndex < StackSize; ++StackIndex)
	{
		UAsceCameraMode* CameraMode = CameraModeStack[StackIndex];
		check(CameraMode);

		CameraMode->UpdateCameraMode(DeltaTime);

		if (CameraMode->GetBlendWeight() >= 1.0f)
		{
			// Everything below this mode is now irrelevant and can be removed.
			RemoveIndex = (StackIndex + 1);
			RemoveCount = (StackSize - RemoveIndex);
			break;
		}
	}

	if (RemoveCount > 0)
	{
		// Let the camera modes know they being removed from the stack.
		for (int32 StackIndex = RemoveIndex; StackIndex < StackSize; ++StackIndex)
		{
			UAsceCameraMode* CameraMode = CameraModeStack[StackIndex];
			check(CameraMode);

			CameraMode->OnDeactivation();
		}

		CameraModeStack.RemoveAt(RemoveIndex, RemoveCount);
	}
}

void UAsceCameraModeStack::BlendStack(FAsceCameraModeView& OutCameraModeView) const
{
	const int32 StackSize = CameraModeStack.Num();
	if (StackSize <= 0)
	{
		return;
	}

	// Start at the bottom and blend up the stack
	// 从栈底开始，按照各自的权重混合栈中的所有相机模式的view，最后返回
	const UAsceCameraMode* CameraMode = CameraModeStack[StackSize - 1];
	check(CameraMode);

	OutCameraModeView = CameraMode->GetCameraModeView();

	for (int32 StackIndex = (StackSize - 2); StackIndex >= 0; --StackIndex)
	{
		CameraMode = CameraModeStack[StackIndex];
		check(CameraMode);

		OutCameraModeView.Blend(CameraMode->GetCameraModeView(), CameraMode->GetBlendWeight());
	}
}

void UAsceCameraModeStack::DrawDebug(UCanvas* Canvas) const
{
	check(Canvas);

	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;

	DisplayDebugManager.SetDrawColor(FColor::Green);
	DisplayDebugManager.DrawString(FString(TEXT("   --- Camera Modes (Begin) ---")));

	for (const UAsceCameraMode* CameraMode : CameraModeStack)
	{
		check(CameraMode);
		CameraMode->DrawDebug(Canvas);
	}

	DisplayDebugManager.SetDrawColor(FColor::Green);
	DisplayDebugManager.DrawString(FString::Printf(TEXT("   --- Camera Modes (End) ---")));
}

void UAsceCameraModeStack::GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const
{
	//返回当前TopLayer即栈底稳定相机模式二点混合权重（似乎一直为1）以及Tag
	if (CameraModeStack.Num() == 0)
	{
		OutWeightOfTopLayer = 1.0f;
		OutTagOfTopLayer = FGameplayTag();
		return;
	}
	else
	{
		UAsceCameraMode* TopEntry = CameraModeStack.Last();
		check(TopEntry);
		OutWeightOfTopLayer = TopEntry->GetBlendWeight();
		OutTagOfTopLayer = TopEntry->GetCameraTypeTag();
	}
}

