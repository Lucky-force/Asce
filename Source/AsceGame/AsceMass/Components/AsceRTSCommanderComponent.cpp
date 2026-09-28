// Fill out your copyright notice in the Description page of Project Settings.
#include "AsceRTSCommanderComponent.h"

#include "AsceGameplayTags.h"
#include "MassEntitySubsystem.h"
#include "Character/AsceObserverComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "GenericTeamAgentInterface.h"
#include "Teams/AsceTeamAgentInterface.h"   // GenericTeamIdToInteger
#include "GameModes/AsceGameState.h"        // 本端阵营表（TeamId → 本端 index）
#include "Math/NumericLimits.h"
#include "AsceMass/Fragments/AsceOrderFragments.h"
#include "AsceMass/FlowField/AscePathFindingSubsystem.h"
#include "AsceMass/Movement/AsceMoveOrderExecutor.h"
#include "AsceMass/Networking/AsceOrderChannelComponent.h"
#include "AsceMass/SpatialGrid/AsceSpatialGridSubsystem.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"
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
    SpatialGrid = World ? World->GetSubsystem<UAsceSpatialGridSubsystem>() : nullptr;
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

int32 UAsceRTSCommanderComponent::GetLocalTeamId()
{
    APawn* Pawn = GetPawn<APawn>();
    AController* Controller = Pawn ? Pawn->GetController() : nullptr;
    // 团队身份来源：Controller（没有则退回 Pawn）的 GenericTeamId；NoTeam/非法 → 0 中立
    IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(Controller);
    if (!TeamAgent && Pawn)
    {
        TeamAgent = Cast<IGenericTeamAgentInterface>(Pawn);
    }
    return TeamAgent ? AsceNormalizeTeamId(GenericTeamIdToInteger(TeamAgent->GetGenericTeamId())) : AsceNeutralTeamId;
}

int32 UAsceRTSCommanderComponent::GetLocalTeamIndex()
{
    // 空间层的"列"是**本端** index ⇒ 必须经本端阵营表换算（不能直接用 TeamId）
    const AAsceGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AAsceGameState>() : nullptr;
    return GameState ? GameState->FindTeamIndex(GetLocalTeamId()) : 0;
}

bool UAsceRTSCommanderComponent::BuildLocalVisionFilter(UAsceFogSubsystem*& OutFog, int32& OutTeamKey)
{
    OutFog = nullptr;
    OutTeamKey = 0;

    if (!EntityManager.IsValid())
    {
        return false;
    }
    UWorld* World = GetWorld();
    if (!World || World->GetNetMode() == NM_Client)
    {
        return false;   // 客户端：敌人实体已被复制裁剪，本地不需要再查迷雾
    }
    UAsceFogSubsystem* Fog = UAsceFogSubsystem::Get(this);
    if (!Fog || !Fog->IsReady())
    {
        return false;
    }
    AAsceGameState* GameState = World->GetGameState<AAsceGameState>();
    if (!GameState)
    {
        return false;
    }
    const int32 TeamKey = GameState->GetTeamKey(GetLocalTeamId());
    if (TeamKey == 0)
    {
        return false;   // 中立/未登记队伍：不裁（安全默认）
    }

    OutFog = Fog;
    OutTeamKey = TeamKey;
    return true;
}

FMassEntityHandle UAsceRTSCommanderComponent::PickVisibleEntityAtScreenPoint(const FVector2D& ScreenPos, APlayerController* Controller)
{
    if (!SpatialGrid.IsValid() || !IsValid(Controller))
    {
        return FMassEntityHandle();
    }

    UAsceFogSubsystem* Fog = nullptr;
    int32 TeamKey = 0;
    if (BuildLocalVisionFilter(Fog, TeamKey))
    {
        auto FilterFn = [this, Fog, TeamKey](FMassEntityHandle Handle)
        {
            return Fog->IsEntityVisibleToLocalTeam(*EntityManager, Handle, TeamKey);
        };
        const FAsceGridEntityFilter Filter(FilterFn);
        return SpatialGrid->FindEntitiesAtScreenPoint(ScreenPos, 100000.f, Controller, true, true, &Filter);
    }
    return SpatialGrid->FindEntitiesAtScreenPoint(ScreenPos, 100000.f, Controller);
}

void UAsceRTSCommanderComponent::QueryVisibleEntitiesInScreenRect(const FVector2D& Start, const FVector2D& End, APlayerController* Controller, int32 TeamFilter, TArray<FMassEntityHandle>& OutResult)
{
    OutResult.Reset();
    if (!SpatialGrid.IsValid() || !IsValid(Controller))
    {
        return;
    }

    UAsceFogSubsystem* Fog = nullptr;
    int32 TeamKey = 0;
    if (BuildLocalVisionFilter(Fog, TeamKey))
    {
        auto FilterFn = [this, Fog, TeamKey](FMassEntityHandle Handle)
        {
            return Fog->IsEntityVisibleToLocalTeam(*EntityManager, Handle, TeamKey);
        };
        const FAsceGridEntityFilter Filter(FilterFn);
        SpatialGrid->FindEntitiesScreen(Start, End, Controller, OutResult, true, true, TeamFilter, &Filter);
        return;
    }
    SpatialGrid->FindEntitiesScreen(Start, End, Controller, OutResult, true, true, TeamFilter);
}

FMassEntityHandle UAsceRTSCommanderComponent::FindNearestOtherTeamEntityInScreenRect(const FVector2D& Start, const FVector2D& End)
{
    APawn* Pawn = GetPawn<APawn>();
    APlayerController* PlayerController = Pawn ? Pawn->GetController<APlayerController>() : nullptr;
    if (!SpatialGrid.IsValid() || !IsValid(PlayerController) || !EntityManager.IsValid())
    {
        return FMassEntityHandle();
    }

    // 不过滤阵营地拿框内全部候选（单位 + 建筑；本地视野过滤在查询里生效），再挑"非己方里离 Start 屏幕坐标最近"的那个
    TArray<FMassEntityHandle> Candidates;
    QueryVisibleEntitiesInScreenRect(Start, End, PlayerController, INDEX_NONE, Candidates);

    const int32 MyTeamId = GetLocalTeamId();
    FMassEntityHandle BestHandle;
    float BestDistSq = TNumericLimits<float>::Max();
    for (const FMassEntityHandle Handle : Candidates)
    {
        if (!EntityManager->IsEntityValid(Handle))
        {
            continue;
        }
        const FAsceTeamFragment* Team = EntityManager->GetFragmentDataPtr<FAsceTeamFragment>(Handle);
        // 敌我比较用 **TeamId**（跨端一致；TeamIndex 只是本端下标，不能比较）
        if (!Team || AsceTeamId(Team->TeamKey) == MyTeamId)
        {
            continue;   // 只挑非己方
        }
        const FTransformFragment* Transform = EntityManager->GetFragmentDataPtr<FTransformFragment>(Handle);
        if (!Transform)
        {
            continue;
        }
        // 屏幕距离（不是世界距离）：投影到屏幕再和 Start 比
        FVector2D ScreenPos;
        if (!PlayerController->ProjectWorldLocationToScreen(Transform->GetTransform().GetLocation(), ScreenPos))
        {
            continue;
        }
        const float DistSq = FVector2D::DistSquared(ScreenPos, Start);
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            BestHandle = Handle;
        }
    }
    return BestHandle;
}

void UAsceRTSCommanderComponent::ConvertEntityNumbersToHandles(const TArray<int64>& EntityNumbers, TArray<FMassEntityHandle>& OutHandles) const
{
    OutHandles.Reset();
    OutHandles.Reserve(EntityNumbers.Num());
    for (const int64 EntityNumber : EntityNumbers)
    {
        const FMassEntityHandle Handle = FMassEntityHandle::FromNumber(static_cast<uint64>(EntityNumber));
        if (!Handle.IsValid())
        {
            continue;
        }
        if (EntityManager.IsValid() && !EntityManager->IsEntityValid(Handle))
        {
            continue;   // 本端已不存在的实体（死亡/被裁剪）
        }
        OutHandles.Add(Handle);
    }
}

void UAsceRTSCommanderComponent::ReplaceSelection(const TArray<FMassEntityHandle>& Handles)
{
    SelectedEntities.Reset();
    SelectedEntities.Reserve(Handles.Num());
    for (const FMassEntityHandle Handle : Handles)
    {
        if (!Handle.IsValid() || SelectedEntities.Contains(Handle))
        {
            continue;
        }
        if (EntityManager.IsValid() && !EntityManager->IsEntityValid(Handle))
        {
            continue;
        }
        SelectedEntities.Add(Handle);
    }
    BroadcastSelectionChanged();
}

void UAsceRTSCommanderComponent::AppendSelection(const TArray<FMassEntityHandle>& Handles)
{
    for (const FMassEntityHandle Handle : Handles)
    {
        if (!Handle.IsValid() || SelectedEntities.Contains(Handle))
        {
            continue;
        }
        if (EntityManager.IsValid() && !EntityManager->IsEntityValid(Handle))
        {
            continue;
        }
        SelectedEntities.Add(Handle);
    }
    BroadcastSelectionChanged();
}

void UAsceRTSCommanderComponent::OnClickSelect(const FVector2D& ScreenPos)
{
    TArray<FMassEntityHandle> NewSelection;
    APawn* Pawn = GetPawn<APawn>();
    if (IsValid(Pawn))
    {
        APlayerController* PlayerController = Pawn->GetController<APlayerController>();
        const FMassEntityHandle EntityHandle = PickVisibleEntityAtScreenPoint(ScreenPos, PlayerController);
        if (EntityHandle.IsValid())
        {
            NewSelection.Add(EntityHandle);
        }
    }
    ReplaceSelection(NewSelection);
}

void UAsceRTSCommanderComponent::OnDragSelect(const FVector2D& Start, const FVector2D& End)
{
    TArray<FMassEntityHandle> NewSelection;
    APawn* Pawn = GetPawn<APawn>();
    if (IsValid(Pawn))
    {
        APlayerController* PlayerController = Pawn->GetController<APlayerController>();
        if (IsValid(PlayerController))
        {
            // 框选：**只收自己方**（单位 + 建筑；阵营过滤在网格查询里做；本地视野过滤在查询里生效）
            QueryVisibleEntitiesInScreenRect(Start, End, PlayerController, GetLocalTeamIndex(), NewSelection);
            if (NewSelection.Num() == 0)
            {
                // 框里只有敌方 ⇒ 视作对"离框选起点最近的非己方"的点选（用于查看敌人信息）
                const FMassEntityHandle Enemy = FindNearestOtherTeamEntityInScreenRect(Start, End);
                if (Enemy.IsValid())
                {
                    NewSelection.Add(Enemy);
                }
            }
        }
    }
    ReplaceSelection(NewSelection);
}

void UAsceRTSCommanderComponent::OnAdditiveClickSelect(const FVector2D& ScreenPos)
{
    TArray<FMassEntityHandle> Hits;
    APawn* Pawn = GetPawn<APawn>();
    if (IsValid(Pawn))
    {
        APlayerController* PlayerController = Pawn->GetController<APlayerController>();
        const FMassEntityHandle EntityHandle = PickVisibleEntityAtScreenPoint(ScreenPos, PlayerController);
        if (EntityHandle.IsValid())
        {
            Hits.Add(EntityHandle);
        }
    }
    AppendSelection(Hits);
}

void UAsceRTSCommanderComponent::OnAdditiveDragSelect(const FVector2D& Start, const FVector2D& End)
{
    TArray<FMassEntityHandle> Hits;
    APawn* Pawn = GetPawn<APawn>();
    if (IsValid(Pawn))
    {
        APlayerController* PlayerController = Pawn->GetController<APlayerController>();
        if (IsValid(PlayerController))
        {
            // 追加框选：同样只收自己方；**不做回退**（框里只有敌方 = 空操作）；本地视野过滤在查询里生效
            QueryVisibleEntitiesInScreenRect(Start, End, PlayerController, GetLocalTeamIndex(), Hits);
        }
    }
    AppendSelection(Hits);
}




void UAsceRTSCommanderComponent::ClearSelection()
{
    SelectedEntities.Empty();
    BroadcastSelectionChanged();
}

void UAsceRTSCommanderComponent::SelectEntities(TArray<int64>& EntityHandles)
{
    TArray<FMassEntityHandle> Handles;
    ConvertEntityNumbersToHandles(EntityHandles, Handles);
    ReplaceSelection(Handles);
}

void UAsceRTSCommanderComponent::AddictiveSelectEntities(TArray<int64>& EntityHandles)
{
    TArray<FMassEntityHandle> Handles;
    ConvertEntityNumbersToHandles(EntityHandles, Handles);
    AppendSelection(Handles);
}

// ── 命令 ────────────────────────────────────

void UAsceRTSCommanderComponent::IssueStopOrder()
{
    // `S`：向当前选中单位发停止指令（服务器权威：清移动命令 + 取消施法；**不影响自动攻击开关**）
    APawn* Pawn = GetPawn<APawn>();
    if (Pawn)
    {
        if (UAsceOrderChannelComponent* OrderChannel = Pawn->FindComponentByClass<UAsceOrderChannelComponent>())
        {
            OrderChannel->RequestStopOrder(SelectedEntities);
        }
    }
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

	// 只对**自己方 + 可移动单位**下达移动令（框选可能混入建筑；点选查看的敌方单位也不能指挥）
	const int32 MyTeamId = GetLocalTeamId();
	TArray<FMassEntityHandle> MovableEntities;
	MovableEntities.Reserve(SelectedEntities.Num());
	for (const FMassEntityHandle Handle : SelectedEntities)
	{
		if (!EntityManager->IsEntityValid(Handle) || !FMassEntityView(*EntityManager, Handle).HasTag<FAsceMovableUnitTag>())
		{
			continue;
		}
		// 指挥权限：客户端先过滤非己方（**TeamId** 比较；服务器 RPC 侧还有一层 NotOwned 兜底）
		const FAsceTeamFragment* Team = EntityManager->GetFragmentDataPtr<FAsceTeamFragment>(Handle);
		if (!Team || AsceTeamId(Team->TeamKey) != MyTeamId)
		{
			continue;
		}
		MovableEntities.Add(Handle);
	}
	if (MovableEntities.Num() == 0)
	{
		return;
	}

	// 到达半径转平方（Fragment存的是平方值，见AsceOrderFragments.h说明）
	const float AcceptanceRadiusSquared = AcceptanceRadius * AcceptanceRadius;

	// 若有OrderChannel就当它是唯一入口（走 NetID RPC；客户端本地预测默认关闭，见 AsceOrderChannelComponent），
	// 保证和正式网络链路行为一致；没有OrderChannel时只有权威端（Standalone/听服host）走本地直写兜底。
	APawn* Pawn = GetPawn<APawn>();
	if (Pawn)
	{
		if (UAsceOrderChannelComponent* OrderChannel = Pawn->FindComponentByClass<UAsceOrderChannelComponent>())
		{
			OrderChannel->RequestMoveOrder(MovableEntities, FVector2D(TargetLocation.X, TargetLocation.Y), AcceptanceRadiusSquared);
			return;
		}

		// 纯客户端没有 OrderChannel 时指令无法下发到服务器：直接拒绝，不要再本地直写（那只会有本端能看到移动）
		if (!Pawn->HasAuthority())
		{
			UE_LOG(LogTemp, Warning, TEXT("[Order] 纯客户端 Pawn(%s) 上没有 UAsceOrderChannelComponent，移动指令被忽略"), *GetNameSafe(Pawn));
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
