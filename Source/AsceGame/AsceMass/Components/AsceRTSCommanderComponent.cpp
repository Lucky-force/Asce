// Fill out your copyright notice in the Description page of Project Settings.
#include "AsceRTSCommanderComponent.h"

#include "AsceGameplayTags.h"
#include "MassEntitySubsystem.h"
#include "Character/AsceObserverComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "AsceMass/Fragments/AsceOrderFragments.h"
#include "AsceMass/FlowField/AscePathFindingSubsystem.h"
#include "AsceMass/Movement/AsceMoveOrderExecutor.h"
#include "AsceMass/Networking/AsceOrderChannelComponent.h"
#include "AsceMass/ScriptBridge/AsceMassScriptBridge.h"
#include "AsceMass/SpatialGrid/AsceSpatialGridSubsystem.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceGameplayTags.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "UI/Inspector/AsceInspectorTypes.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRTSCommanderComponent)
const FName UAsceRTSCommanderComponent::NAME_ActorFeatureName("RTSCommander");

bool UAsceRTSCommanderComponent::CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const
{
    // Spawned: 不需要额外条件
    if (!CurrentState.IsValid() && DesiredState == AsceGameplayTags::InitState_Spawned)
    {
        return GetPawn<APawn>() != nullptr;
    }

    // Spawned → DataAvailable: 需要 ObserverComponent 至少到达 DataAvailable
    if (CurrentState == AsceGameplayTags::InitState_Spawned && DesiredState == AsceGameplayTags::InitState_DataAvailable)
    {
        APawn* Pawn = GetPawn<APawn>();
        return Manager->HasFeatureReachedInitState(Pawn, UAsceObserverComponent::NAME_ActorFeatureName,AsceGameplayTags::InitState_DataAvailable);
    }

    // DataAvailable → DataInitialized: 满足即可转
    if (CurrentState == AsceGameplayTags::InitState_DataAvailable &&
        DesiredState == AsceGameplayTags::InitState_DataInitialized)
    {
        return true;
    }

    // DataInitialized → GameplayReady: 满足即可转
    if (CurrentState == AsceGameplayTags::InitState_DataInitialized &&
        DesiredState == AsceGameplayTags::InitState_GameplayReady)
    {
        return true;
    }

    return false;
}

void UAsceRTSCommanderComponent::HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState)
{
    if (CurrentState == AsceGameplayTags::InitState_DataAvailable && DesiredState == AsceGameplayTags::InitState_DataInitialized)
    {
        // Observer 已经 DataAvailable, 现在可以安全绑定委托
        DoInitialize();
    }
}

void UAsceRTSCommanderComponent::OnActorInitStateChanged(const FActorInitStateChangedParams& Params)
{
    if (Params.FeatureName == UAsceObserverComponent::NAME_ActorFeatureName)
    {
        CheckDefaultInitialization();
    }
}

void UAsceRTSCommanderComponent::CheckDefaultInitialization()
{
    static const TArray<FGameplayTag> StateChain = {
        AsceGameplayTags::InitState_Spawned,
        AsceGameplayTags::InitState_DataAvailable,
        AsceGameplayTags::InitState_DataInitialized,
        AsceGameplayTags::InitState_GameplayReady
    };
    ContinueInitStateChain(StateChain);
}

void UAsceRTSCommanderComponent::GetSelectedEntities(TArray<int64>& OutResult) const
{
    for (FMassEntityHandle EntityHandle : SelectedEntities)
    {
        OutResult.Add(EntityHandle.AsNumber());
    }
}

void UAsceRTSCommanderComponent::GetSelectedEntityLocations(TArray<FVector2D>& OutLocations) const
{
    if (!EntityManager.IsValid())
    {
        return;
    }
    OutLocations.Reserve(SelectedEntities.Num());
    for (FMassEntityHandle EntityHandle : SelectedEntities)
    {
        if (!EntityManager->IsEntityValid(EntityHandle))
        {
            continue;
        }
        const FTransformFragment* Transform = EntityManager->GetFragmentDataPtr<FTransformFragment>(EntityHandle);
        if (!Transform)
        {
            continue;
        }
        const FVector Loc = Transform->GetTransform().GetLocation();
        OutLocations.Add(FVector2D(Loc.X, Loc.Y));
    }
}

/** 选中状态变化时统一广播（调用方先改完SelectedEntities再调） */
void UAsceRTSCommanderComponent::BroadcastSelectionChanged()
{
    PrintSelectedEntities();
    OnSelectionChanged.Broadcast();

    // UI 解耦通知：选择集合变化（含主单位 = 第一个的信息）
    FAsceSelectionChangedMessage Message;
    Message.Count = SelectedEntities.Num();
    if (SelectedEntities.Num() > 0)
    {
        Message.PrimaryEntityNumber = SelectedEntities[0].AsNumber();
        if (EntityManager.IsValid())
        {
            if (const FAsceUnitDefFragment* Def = EntityManager->GetFragmentDataPtr<FAsceUnitDefFragment>(SelectedEntities[0]))
            {
                Message.PrimaryTypeId = Def->UniqueTypeId;
            }
        }
    }
    if (UGameplayMessageSubsystem::HasInstance(this))
    {
        UGameplayMessageSubsystem::Get(this).BroadcastMessage(AsceGameplayTags::UI_SelectionChanged, Message);
    }
}

UAsceRTSCommanderComponent::UAsceRTSCommanderComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UAsceRTSCommanderComponent::BeginPlay()
{
    Super::BeginPlay();
    
    // 监听 ObserverComponent 的状态变化 (Observer 就绪后才能绑定委托)
    BindOnActorInitStateChanged(UAsceObserverComponent::NAME_ActorFeatureName, FGameplayTag(), false);

    // 进入 Spawned → 触发 CheckDefaultInitialization 推进状态链
    ensure(TryToChangeInitState(AsceGameplayTags::InitState_Spawned));
    CheckDefaultInitialization();
}

void UAsceRTSCommanderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // 解绑
    DoUninitialize();
    UnregisterInitStateFeature();
    Super::EndPlay(EndPlayReason);
}

void UAsceRTSCommanderComponent::DoInitialize()
{
    CacheSubsystems();
    BindToObserverComponent();

    UWorld* World = GetWorld();
    if (IsValid(World))
    {
        if (UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>())
        {
            EntityManager = EntitySubsystem->GetMutableEntityManager().AsShared();
        }
    }
}

void UAsceRTSCommanderComponent::DoUninitialize()
{
    UnbindFromObserverComponent();
    SelectedEntities.Empty();
    EntityManager.Reset();
}


// ── 初始化 ────────────────────────────────────

void UAsceRTSCommanderComponent::CacheSubsystems()
{
    UWorld* World = GetWorld();
    ScriptBridge = World ? World->GetSubsystem<UAsceMassScriptBridge>() : nullptr;
    SpatialGrid  = World ? World->GetSubsystem<UAsceSpatialGridSubsystem>() : nullptr;
}

void UAsceRTSCommanderComponent::BindToObserverComponent()
{
    APawn* Pawn = GetPawn<APawn>();
    if (!IsValid(Pawn))
    {
        return;
    }

    ObserverComp = Pawn->FindComponentByClass<UAsceObserverComponent>();
    if (!ObserverComp.IsValid())
    {
        return;
    }

    OnSelectDelegateHandle = ObserverComp->OnSelect.AddUObject(this, &UAsceRTSCommanderComponent::OnClickSelect);
    OnDragSelectDelegateHandle = ObserverComp->OnDragSelect.AddUObject(this, &UAsceRTSCommanderComponent::OnDragSelect);
    OnAdditiveSelectDelegateHandle = ObserverComp->OnAdditiveSelect.AddUObject(this, &UAsceRTSCommanderComponent::OnAdditiveClickSelect);
    OnAdditiveDragSelectDelegateHandle = ObserverComp->OnAdditiveDragSelect.AddUObject(this, &UAsceRTSCommanderComponent::OnAdditiveDragSelect);
    OnMoveOrderDelegateHandle = ObserverComp->OnMoveOrder.AddUObject(this, &UAsceRTSCommanderComponent::OnMoveOrder);
}

void UAsceRTSCommanderComponent::UnbindFromObserverComponent()
{
    if (!ObserverComp.IsValid())
    {
        return;
    }

    ObserverComp->OnSelect.Remove(OnSelectDelegateHandle);
    ObserverComp->OnDragSelect.Remove(OnDragSelectDelegateHandle);
    ObserverComp->OnAdditiveSelect.Remove(OnAdditiveSelectDelegateHandle);
    ObserverComp->OnAdditiveDragSelect.Remove(OnAdditiveDragSelectDelegateHandle);
    ObserverComp->OnMoveOrder.Remove(OnMoveOrderDelegateHandle);
}

// ── 委托回调 ──────────────────────────────────

void UAsceRTSCommanderComponent::OnClickSelect(const FVector2D& ScreenPos)
{
    SelectedEntities.Reset();
    APawn* Pawn = GetPawn<APawn>();
    if (IsValid(Pawn))
    {
        APlayerController* PlayerController = Pawn->GetController<APlayerController>();
        if (SpatialGrid.IsValid() && IsValid(PlayerController))
        {
            FMassEntityHandle EntityHandle = SpatialGrid->FindEntitiesAtScreenPoint(ScreenPos, 100000.f, PlayerController);
            if (EntityHandle.IsValid())
            {
                SelectedEntities.Add(EntityHandle);
            }
        }
    }
    BroadcastSelectionChanged();
}

void UAsceRTSCommanderComponent::OnDragSelect(const FVector2D& Start, const FVector2D& End)
{
    SelectedEntities.Reset();
    APawn* Pawn = GetPawn<APawn>();
    if (IsValid(Pawn))
    {
        APlayerController* PlayerController = Pawn->GetController<APlayerController>();
        if (SpatialGrid.IsValid() && IsValid(PlayerController))
        {
            SpatialGrid->FindEntitiesScreen(Start, End, PlayerController, SelectedEntities);
        }
    }
    BroadcastSelectionChanged();
}

void UAsceRTSCommanderComponent::OnAdditiveClickSelect(const FVector2D& ScreenPos)
{
    APawn* Pawn = GetPawn<APawn>();
    if (!IsValid(Pawn))
    {
        BroadcastSelectionChanged();
        return;
    }
    
    APlayerController* PlayerController = Pawn->GetController<APlayerController>();
    if (SpatialGrid.IsValid() && IsValid(PlayerController))
    {
        FMassEntityHandle EntityHandle = SpatialGrid->FindEntitiesAtScreenPoint(ScreenPos, 100000.f, PlayerController);
        if (EntityHandle.IsValid())
        {
            SelectedEntities.Add(EntityHandle);
        }
    }
    BroadcastSelectionChanged();
}

void UAsceRTSCommanderComponent::OnAdditiveDragSelect(const FVector2D& Start, const FVector2D& End)
{
    APawn* Pawn = GetPawn<APawn>();
    if (!IsValid(Pawn))
    {
        BroadcastSelectionChanged();
        return;
    }
    
    APlayerController* PlayerController = Pawn->GetController<APlayerController>();
    if (SpatialGrid.IsValid() && IsValid(PlayerController))
    {
        TArray<FMassEntityHandle> EntityHandles;
        SpatialGrid->FindEntitiesScreen(Start, End, PlayerController, EntityHandles);
        SelectedEntities.Append(EntityHandles);
    }
    BroadcastSelectionChanged();
}




void UAsceRTSCommanderComponent::ClearSelection()
{
    SelectedEntities.Empty();
    BroadcastSelectionChanged();
}

void UAsceRTSCommanderComponent::SelectEntities(TArray<int64>& EntityHandles)
{
}

void UAsceRTSCommanderComponent::AddictiveSelectEntities(TArray<int64>& EntityHandles)
{
}

// ── 命令 ────────────────────────────────────

void UAsceRTSCommanderComponent::IssueAttackOrder(int64 Target)
{
}

void UAsceRTSCommanderComponent::IssueStopOrder()
{

}

void UAsceRTSCommanderComponent::OnRegister()
{
    Super::OnRegister();
    
    // 必须是 Pawn 上的组件
    if (GetPawn<APawn>())
    {
        RegisterInitStateFeature();
    }
}


void UAsceRTSCommanderComponent::OnMoveOrder(const FVector2D& ScreenPos)
{
	APawn* Pawn = GetPawn<APawn>();
	APlayerController* PC = Pawn ? Pawn->GetController<APlayerController>() : nullptr;
	UWorld* World = GetWorld();
	if (!PC || !World)
	{
		return;
	}

	// 屏幕坐标反投影成世界射线，再做一步物理Channel射线找回落地世界坐标
	// （走物理层而不是平面相交，让地面/阻挡物都能挡住指令落点）。
	FVector RayOrigin;
	FVector RayDirection;
	if (!PC->DeprojectScreenPositionToWorld(ScreenPos.X, ScreenPos.Y, RayOrigin, RayDirection))
	{
		return;
	}

	FHitResult HitResult;
	const FVector RayEnd = RayOrigin + RayDirection * 100000.f;
	if (!World->LineTraceSingleByChannel(HitResult, RayOrigin, RayEnd, ECC_Visibility))
	{
		return;
	}

	IssueMoveOrder(HitResult.ImpactPoint, MoveOrderAcceptanceRadius);
}

void UAsceRTSCommanderComponent::IssueMoveOrder(const FVector& TargetLocation, const float AcceptanceRadius)
{
	if (!EntityManager.IsValid())
	{
		return;
	}

	// 只对**可移动单位**下达移动令（框选可能混入建筑等静态实体）
	TArray<FMassEntityHandle> MovableEntities;
	MovableEntities.Reserve(SelectedEntities.Num());
	for (const FMassEntityHandle Handle : SelectedEntities)
	{
		if (EntityManager->IsEntityValid(Handle) && FMassEntityView(*EntityManager, Handle).HasTag<FAsceMovableUnitTag>())
		{
			MovableEntities.Add(Handle);
		}
	}
	if (MovableEntities.Num() == 0)
	{
		return;
	}

	// 到达半径转平方（Fragment存的是平方值，见AsceOrderFragments.h说明）
	const float AcceptanceRadiusSquared = AcceptanceRadius * AcceptanceRadius;

	// 若有OrderChannel就当它是唯一入口（它会走NetID RPC + 客户端本地预测），
	// 保证和正式网络链路行为一致；没有就退化为本地直写，让Standalone先跑通。
	APawn* Pawn = GetPawn<APawn>();
	if (Pawn)
	{
		if (UAsceOrderChannelComponent* OrderChannel = Pawn->FindComponentByClass<UAsceOrderChannelComponent>())
		{
			OrderChannel->RequestMoveOrder(MovableEntities, FVector2D(TargetLocation.X, TargetLocation.Y), AcceptanceRadiusSquared);
			return;
		}
	}

	// ── 本地直写（无OrderChannel的Standalone路径）──
	TArray<FMassEntityHandle> ValidHandles;
	TArray<FVector2D> EntityWorldLocations2D;
	ValidHandles.Reserve(MovableEntities.Num());
	EntityWorldLocations2D.Reserve(MovableEntities.Num());

	for (FMassEntityHandle Handle : MovableEntities)
	{
		if (!EntityManager->IsEntityValid(Handle))
		{
			continue;
		}
		const FAsceMoveOrderFragment* MoveOrder = EntityManager->GetFragmentDataPtr<FAsceMoveOrderFragment>(Handle);
		const FTransformFragment* TransformFragment = EntityManager->GetFragmentDataPtr<FTransformFragment>(Handle);
		if (!MoveOrder || !TransformFragment)
		{
			continue;
		}
		const FVector CurrentLocation = TransformFragment->GetTransform().GetLocation();
		ValidHandles.Add(Handle);
		EntityWorldLocations2D.Add(FVector2D(CurrentLocation.X, CurrentLocation.Y));
	}

	if (ValidHandles.Num() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	UAscePathFindingSubsystem* PathFindingSubsystem = World ? World->GetSubsystem<UAscePathFindingSubsystem>() : nullptr;
	UPathFinder* PathFinder = PathFindingSubsystem ? PathFindingSubsystem->GetPathFinder() : nullptr;
	if (!PathFinder)
	{
		return;
	}

	// 批量寻路 + 分配Handle + 写Fragment（同OrderChannel/Server共享的实现）
	FAsceMoveOrderExecutor::ApplyMergedMoveOrder(
		*EntityManager.Get(),
		*PathFinder,
		ValidHandles,
		EntityWorldLocations2D,
		FVector2D(TargetLocation.X, TargetLocation.Y),
		AcceptanceRadiusSquared,
		++LocalOrderSequence);
}