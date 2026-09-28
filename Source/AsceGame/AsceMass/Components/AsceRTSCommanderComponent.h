// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassEntityHandle.h"
#include "Components/PawnComponent.h"
#include "MassEntityTypes.h"
#include "Components/GameFrameworkInitStateInterface.h"
#include "AsceRTSCommanderComponent.generated.h"
#define UE_API ASCEGAME_API
class APlayerController;
class UAsceObserverComponent;
class UAsceSpatialGridSubsystem;
class UAsceFogSubsystem;

DECLARE_MULTICAST_DELEGATE(FOnSelectionChanged)

UCLASS(MinimalAPI, Blueprintable, meta=(BlueprintSpawnableComponent))
class UAsceRTSCommanderComponent : public UPawnComponent, public IGameFrameworkInitStateInterface
{
    GENERATED_BODY()

public:
    UAsceRTSCommanderComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    // ── Feature name ──
    static UE_API const FName NAME_ActorFeatureName;

    //~ Begin IGameFrameworkInitStateInterface
    virtual FName GetFeatureName() const override { return NAME_ActorFeatureName; }
    UE_API virtual bool CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const override;
    UE_API virtual void HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) override;
    UE_API virtual void OnActorInitStateChanged(const FActorInitStateChangedParams& Params) override;
    UE_API virtual void CheckDefaultInitialization() override;
    //~ End IGameFrameworkInitStateInterface
    
    // ── 选中状态 ──
// ── 选中状态 ──
    UFUNCTION(BlueprintCallable)
    void GetSelectedEntities(TArray<int64>& OutResult) const;

    // 拿到选中entity的世界位置(2D)，供Debugger/UI等消费者读取选中内容
    void GetSelectedEntityLocations(TArray<FVector2D>& OutLocations) const;

    UFUNCTION(BlueprintCallable)
    void ClearSelection();
    
    UFUNCTION(BlueprintCallable)
    void SelectEntities(TArray<int64>& EntityHandles);
    
    UFUNCTION(BlueprintCallable)
    void AddictiveSelectEntities(TArray<int64>& EntityHandles);

    // 选中状态变化广播（框选/点击/清空都会触发）：Debugger等消费者监听刷新source
    FOnSelectionChanged OnSelectionChanged;

    // ── 命令下发 ──
    UFUNCTION(BlueprintCallable)
    void IssueMoveOrder(const FVector& WorldTarget, const float AcceptanceRadius);

    UFUNCTION(BlueprintCallable)
    void IssueStopOrder();

protected:
    UE_API virtual void OnRegister() override;
    UE_API virtual void BeginPlay() override;
    UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    FORCEINLINE virtual void PrintSelectedEntities()
    {
#if WITH_EDITOR
        FString SelectedEntitiesString;
        for (int i = 0; i < SelectedEntities.Num(); i++)
        {   
            SelectedEntitiesString.Append( FString::FromInt(SelectedEntities[i].Index));
            SelectedEntitiesString.Append(",");
        }
        UE_LOG(LogTemp, Warning, TEXT("CommandComp: Selected Entities : %s"),*SelectedEntitiesString);
#endif
    };
    
private:
    // ── 真正的初始化工作 (在 HandleChangeInitState → DataInitialized 中调用) ──
    void DoInitialize();
    void DoUninitialize();
    
    // ── 处理输入组件的委托回调 ──
    void OnClickSelect(const FVector2D& ScreenPos);
    void OnDragSelect(const FVector2D& Start, const FVector2D& End);
    void OnAdditiveClickSelect(const FVector2D& ScreenPos);
    void OnAdditiveDragSelect(const FVector2D& Start, const FVector2D& End);
    void OnMoveOrder(const FVector2D& ScreenPos);

    // ── 内部 ──
    void BindToObserverComponent();
    void UnbindFromObserverComponent();
    void CacheSubsystems();

    // 选中状态变化统一出口：打印当前选中 + 广播OnSelectionChanged（调用方改完SelectedEntities后调）
    void BroadcastSelectionChanged();

    // 选中状态统一入口（过滤无效 handle + 去重 → 最后统一 BroadcastSelectionChanged）：
    // 输入回调与程序化选中 API（SelectEntities/AddictiveSelectEntities）共用，避免各处漏去重/漏广播。
    void ReplaceSelection(const TArray<FMassEntityHandle>& Handles);
    void AppendSelection(const TArray<FMassEntityHandle>& Handles);

    // int64（`FMassEntityHandle::AsNumber()`）批量转成本端 handle，过滤本端无效实体
    void ConvertEntityNumbersToHandles(const TArray<int64>& EntityNumbers, TArray<FMassEntityHandle>& OutHandles) const;

    // 本地玩家**团队身份**（跨端一致，来自 Controller 的 GenericTeamId；NoTeam/非法 → 0 中立）
    // 敌我/指挥权限比较一律用它
    int32 GetLocalTeamId();

    // 本地玩家在**本端阵营表**里的列（空间层过滤用；= GameState->FindTeamIndex(GetLocalTeamId())）
    int32 GetLocalTeamIndex();

    // 框选回退：屏幕矩形内"离 Start 屏幕坐标最近的非己方"（框里只有敌人时当点选用；找不到 → 无效 handle）
    FMassEntityHandle FindNearestOtherTeamEntityInScreenRect(const FVector2D& Start, const FVector2D& End);

    // ── 本地视野过滤（选择侧；B1）──
    /** 本端是否需要按迷雾过滤交互查询：客户端不需要（实体已被复制裁剪）；standalone/听服 host 需要。
     *  成功时输出 Fog 与本地队伍 Key（打包 TeamKey）。 */
    bool BuildLocalVisionFilter(UAsceFogSubsystem*& OutFog, int32& OutTeamKey);
    /** 点选：取"对本队可见"的最近命中（雾里的敌人会被跳过、继续找后面的候选）。 */
    FMassEntityHandle PickVisibleEntityAtScreenPoint(const FVector2D& ScreenPos, APlayerController* Controller);
    /** 框选：带可选视野过滤的屏幕矩形查询（TeamFilter 语义同网格 API）。 */
    void QueryVisibleEntitiesInScreenRect(const FVector2D& Start, const FVector2D& End, APlayerController* Controller, int32 TeamFilter, TArray<FMassEntityHandle>& OutResult);

    // ── 缓存 ──
    UPROPERTY()
    TWeakObjectPtr<UAsceObserverComponent> ObserverComp;

    UPROPERTY()
    TWeakObjectPtr<UAsceSpatialGridSubsystem> SpatialGrid;

    // ── 当前选中的entity ──
    TArray<FMassEntityHandle> SelectedEntities;
    
    TSharedPtr<FMassEntityManager> EntityManager;

    // 本地直写路径（无OrderChannel）的下单序号分配器，语义同UAsceOrderChannelComponent::NextOrderSequence，
    // 仅用于拒绝回执的本地关联（当前直写路径无回执，递增保持语义一致）。
    int32 LocalOrderSequence = 0;
    
    FDelegateHandle OnSelectDelegateHandle;
    FDelegateHandle OnDragSelectDelegateHandle; 
    FDelegateHandle OnAdditiveSelectDelegateHandle;
    FDelegateHandle OnAdditiveDragSelectDelegateHandle;
    FDelegateHandle OnMoveOrderDelegateHandle;

    // ── 右键移动到达半径（非平方，传给IssueMoveOrder后再平方）──
    UPROPERTY(EditAnywhere, Category = "Asce|RTSCommander")
    float MoveOrderAcceptanceRadius = 50.f;
    
};
#undef UE_API
